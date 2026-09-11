/* Draws pixels, text, and the mouse pointer. */
#include "os.h"
#include "vbank.inc"
#include "fbspan.inc"
#include "panic_report.inc"

int SW, SH, SPITCH;
static u8 *lfb;
static u8 *flip_shadow;
static int shadow_valid, shadow_attempted;
u32 fb_copy_bytes, fb_skip_bytes;

static int vb_scheme = VB_NONE;
static int vb_bank = -1;

static void vb_exec(const VbWrite *w, int n)
{
    for (int i = 0; i < n; i++) {
        if (w[i].op == VBOP_OUT) outb(w[i].port, w[i].val);
        else {
            outb(w[i].port, w[i].idx);
            if (w[i].op == VBOP_IDX) outb(w[i].port + 1, w[i].val);
            else (void)inb(w[i].port + 1);
        }
    }
}

static void vb_set_bank(int bank)
{
    VbWrite w[8];
    vb_exec(w, vbank_prog(vb_scheme, bank, w));
    vb_bank = bank;
}

static int vb_find_scheme(void)
{
    for (int bus = 0; bus < 4; bus++)
        for (int dev = 0; dev < 32; dev++) {
            u32 a = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11);
            outl(0xCF8, a);
            u32 id = inl(0xCFC);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            outl(0xCF8, a | 8);
            if ((inl(0xCFC) >> 16) != 0x0300) continue;
            int s = vbank_detect((u16)id);
            if (s != VB_NONE) return s;
        }
    outb(0x3C4, 0x06); outb(0x3C5, 0x12);
    outb(0x3C4, 0x06);
    if (inb(0x3C5) == 0x12) return VB_CIRRUS;
    outb(0x3D4, 0x38); outb(0x3D5, 0x48);
    outb(0x3D4, 0x30);
    u8 s3id = inb(0x3D5);
    if (s3id >= 0x80 && s3id != 0xFF) return VB_S3;
    outb(0x3BF, 0x03); outb(0x3D8, 0xA0);
    u8 old3cd = inb(0x3CD);
    outb(0x3CD, 0x55);
    u8 et = inb(0x3CD);
    outb(0x3CD, old3cd);
    if (et == 0x55) return VB_TSENG;
    outb(0x3C4, 0x0B);
    u8 tv = inb(0x3C5);
    if (tv && tv != 0xFF) return VB_TRIDENT;
    return VB_NONE;
}

static void vga13h_set(void)
{
    static const u8 seq[5]  = { 0x03,0x01,0x0F,0x00,0x0E };
    static const u8 crtc[25]= { 0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
                                0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
                                0x9C,0x8E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF };
    static const u8 gc[9]   = { 0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF };
    static const u8 attr[21]= { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                                0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
                                0x41,0x00,0x0F,0x00,0x00 };
    outb(0x3C2, 0x63);
    for (int i = 0; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, seq[i]); }
    outb(0x3D4, 0x11); outb(0x3D5, 0x0E);
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, crtc[i]); }
    for (int i = 0; i < 9; i++)  { outb(0x3CE, i); outb(0x3CF, gc[i]); }
    for (int i = 0; i < 21; i++) {
        (void)inb(0x3DA);
        outb(0x3C0, i); outb(0x3C0, attr[i]);
    }
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}

static const u8 pal16[16][3] = {
    {0,0,0},   {0,0,26},  {0,26,0},  {0,26,26},
    {26,0,0},  {26,0,26}, {26,26,0}, {42,42,42},
    {21,21,21},{16,16,63},{16,58,16},{16,58,58},
    {60,16,16},{58,16,58},{63,58,16},{63,63,63}
};

static u8 pal_rgb[256][3];

static void dac(u8 idx, u8 r, u8 g, u8 b)
{
    outb(0x3C8, idx);
    outb(0x3C9, r); outb(0x3C9, g); outb(0x3C9, b);
    pal_rgb[idx][0] = (r << 2) | (r >> 4);
    pal_rgb[idx][1] = (g << 2) | (g >> 4);
    pal_rgb[idx][2] = (b << 2) | (b >> 4);
}

void palette_rgb(int idx, u8 *r, u8 *g, u8 *b)
{
    *r = pal_rgb[idx & 0xFF][0];
    *g = pal_rgb[idx & 0xFF][1];
    *b = pal_rgb[idx & 0xFF][2];
}

u8 palette_nearest(u8 r, u8 g, u8 b)
{
    int best = 0; long bestd = 1L << 30;
    for (int i = 0; i < 256; i++) {
        int dr = r - pal_rgb[i][0], dg = g - pal_rgb[i][1], db = b - pal_rgb[i][2];
        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (d < bestd) { bestd = d; best = i; if (!d) break; }
    }
    return (u8)best;
}

static void set_palette(void)
{
    for (int i = 0; i < 16; i++)
        dac(i, pal16[i][0], pal16[i][1], pal16[i][2]);
    for (int i = 0; i < 8; i++) {
        u8 v = 8 + i * 7;
        dac(C_G0 + i, v, v, v);
    }
    for (int i = 0; i < 8; i++)
        dac(C_TB0 + i, 2 + i, 6 + i * 3, 22 + i * 5);
    dac(C_DESK,   11, 20, 24);
    dac(C_FACE,   46, 46, 47);
    dac(C_LIGHT,  62, 62, 63);
    dac(C_SHAD,   26, 26, 28);
    dac(C_DARK,   10, 10, 12);
    dac(C_TERMBG,  2,  3,  4);
    dac(C_TERMFG, 20, 58, 24);
    dac(C_HILITE, 14, 26, 52);
    for (int i = 48; i < 256; i++) {
        u8 v = (i - 48) * 63 / 207;
        dac(i, v, v, v);
    }
}

void gfx_init(void)
{
    if (BOOTINFO->vbe == 2) {
        vb_scheme = vb_find_scheme();
        if (vb_scheme == VB_NONE) {
            vga13h_set();
            BOOTINFO->w = 320; BOOTINFO->h = 200; BOOTINFO->pitch = 320;
            BOOTINFO->lfb = 0xA0000;
            BOOTINFO->vbe = 0;
            BOOTINFO->vbe_mode = 0x13;
        } else {
            vb_set_bank(0);
        }
    }
    SW = BOOTINFO->w;
    SH = BOOTINFO->h;
    SPITCH = BOOTINFO->pitch;
    lfb = (u8 *)BOOTINFO->lfb;

    if (SW < 320 || SW > 2048 || SH < 200 || SH > 1536 || SPITCH < SW ||
        (u32)SW * SH > memory.fb_end - memory.fb) {
        vga13h_set(); BOOTINFO->vbe = 0;
        SW = 320; SH = 200; SPITCH = 320;
        lfb = (u8 *)0xA0000;
    }

    mtrr_init((u32)(u32 *)lfb, (u32)SPITCH * (u32)SH, BOOTINFO->vbe == 2);
    set_palette();
    emergency_video((u32)lfb,SW,SH,SPITCH,BOOTINFO->vbe==2 ? vb_scheme : 0);
    memset(BACKBUF, C_DESK, SW * SH);
}

static int clx0, cly0, clx1 = 1 << 30, cly1 = 1 << 30;

void set_clip(int x, int y, int w, int h)
{
    clx0 = x < 0 ? 0 : x;
    cly0 = y < 0 ? 0 : y;
    clx1 = x + w;
    cly1 = y + h;
}

void clear_clip(void)
{
    clx0 = 0;
    cly0 = 0;
    clx1 = 1 << 30;
    cly1 = 1 << 30;
}

int surface_lock(u8 **px, int *pitch, int *w, int *h)
{
    if (px)    *px = BACKBUF;
    if (pitch) *pitch = SW;
    if (w)     *w = SW;
    if (h)     *h = SH;
    return 1;
}

void surface_unlock(void) { }

void clip_rect_get(int *x, int *y, int *w, int *h)
{
    int x0 = clx0 < 0 ? 0 : clx0, y0 = cly0 < 0 ? 0 : cly0;
    int x1 = clx1 > SW ? SW : clx1, y1 = cly1 > SH ? SH : cly1;
    if (x) *x = x0;
    if (y) *y = y0;
    if (w) *w = x1 > x0 ? x1 - x0 : 0;
    if (h) *h = y1 > y0 ? y1 - y0 : 0;
}

void fill_rect(int x, int y, int w, int h, u8 c)
{
    if (x < clx0) { w += x - clx0; x = clx0; }
    if (y < cly0) { h += y - cly0; y = cly0; }
    if (x + w > clx1) w = clx1 - x;
    if (y + h > cly1) h = cly1 - y;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memset(BACKBUF + (y + j) * SW + x, c, w);
}

void pixel(int x, int y, u8 c)
{
    if (x < clx0 || y < cly0 || x >= clx1 || y >= cly1) return;
    if (x < 0 || y < 0 || x >= SW || y >= SH) return;
    BACKBUF[y * SW + x] = c;
}

u8 getpixel(int x, int y)
{
    if (x < 0 || y < 0 || x >= SW || y >= SH) return 0;
    return BACKBUF[y * SW + x];
}

void line(int x0, int y0, int x1, int y1, u8 c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) return;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void rect(int x, int y, int w, int h, u8 c)
{
    if (w <= 0 || h <= 0) return;
    fill_rect(x, y, w, 1, c);
    fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c);
    fill_rect(x + w - 1, y, 1, h, c);
}

void circle(int cx, int cy, int r, u8 c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pixel(cx + x, cy + y, c); pixel(cx - x, cy + y, c);
        pixel(cx + x, cy - y, c); pixel(cx - x, cy - y, c);
        pixel(cx + y, cy + x, c); pixel(cx - y, cy + x, c);
        pixel(cx + y, cy - x, c); pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fill_circle(int cx, int cy, int r, u8 c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        fill_rect(cx - x, cy + y, 2 * x + 1, 1, c);
        fill_rect(cx - x, cy - y, 2 * x + 1, 1, c);
        fill_rect(cx - y, cy + x, 2 * y + 1, 1, c);
        fill_rect(cx - y, cy - x, 2 * y + 1, 1, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void hline(int x, int y, int w, u8 c) { fill_rect(x, y, w, 1, c); }
void vline(int x, int y, int h, u8 c) { fill_rect(x, y, 1, h, c); }

void bevel(int x, int y, int w, int h, int sunken)
{
    u8 tl = sunken ? C_SHAD : C_LIGHT;
    u8 br = sunken ? C_LIGHT : C_DARK;
    hline(x, y, w, tl);
    vline(x, y, h, tl);
    hline(x, y + h - 1, w, br);
    vline(x + w - 1, y, h, br);
    u8 tl2 = sunken ? C_DARK : C_FACE;
    u8 br2 = sunken ? C_FACE : C_SHAD;
    hline(x + 1, y + 1, w - 2, tl2);
    vline(x + 1, y + 1, h - 2, tl2);
    hline(x + 1, y + h - 2, w - 2, br2);
    vline(x + w - 2, y + 1, h - 2, br2);
}

void panel(int x, int y, int w, int h, int sunken)
{
    fill_rect(x, y, w, h, C_FACE);
    bevel(x, y, w, h, sunken);
}

void draw_char(int x, int y, char ch, u8 fg)
{
    const u8 *g = FONT8x16 + (u8)ch * 16;
    for (int j = 0; j < 16; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH || yy < cly0 || yy >= cly1) continue;
        u8 row = g[j];
        if (!row) continue;
        u8 *dst = BACKBUF + yy * SW;
        for (int i = 0; i < 8; i++) {
            if (!(row & (0x80 >> i))) continue;
            int xx = x + i;
            if (xx >= 0 && xx < SW && xx >= clx0 && xx < clx1) dst[xx] = fg;
        }
    }
}

void draw_text_scaled(int x, int y, const char *s, u8 fg, int sx, int sy)
{
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;
    for (; *s; s++, x += 8 * sx) {
        const u8 *g = FONT8x16 + (u8)*s * 16;
        for (int j = 0; j < 16; j++) {
            u8 row = g[j];
            if (!row) continue;
            for (int i = 0; i < 8; i++)
                if (row & (0x80 >> i))
                    fill_rect(x + i * sx, y + j * sy, sx, sy, fg);
        }
    }
}

const u8 *font_glyph(char ch) { return FONT8x16 + (u8)ch * 16; }

void draw_text(int x, int y, const char *s, u8 fg)
{
    while (*s) {
        draw_char(x, y, *s++, fg);
        x += 8;
    }
}

void draw_text_clip(int x, int y, const char *s, u8 fg, int maxpx)
{
    int cells = maxpx / 8;
    int len = (int)strlen(s);
    if (len <= cells) { draw_text(x, y, s, fg); return; }
    if (cells < 2) return;
    for (int i = 0; i < cells - 2; i++) draw_char(x + i * 8, y, s[i], fg);
    draw_char(x + (cells - 2) * 8, y, '.', fg);
    draw_char(x + (cells - 1) * 8, y, '.', fg);
}

void draw_text_clip2(int x, int y, const char *s, u8 fg, int cx0, int cx1)
{
    for (; *s; s++, x += 8) {
        if (x + 8 > cx1) break;
        if (x < cx0) continue;
        draw_char(x, y, *s, fg);
    }
}

#include "sbar.inc"
static void thumb_geo(int len, int total, int vis, int off, int *tl, int *tp)
{
    sb_thumb(len, total, vis, off, tl, tp);
}

void draw_sbar(int x, int y, int len, int horiz, int total, int vis, int off)
{
    int tl, tp;
    thumb_geo(len, total, vis, off, &tl, &tp);
    if (horiz) {
        fill_rect(x, y, len, SB_W, C_G0 + 6);
        hline(x, y, len, C_SHAD);
        panel(x + tp, y + 1, tl, SB_W - 1, 0);
    } else {
        fill_rect(x, y, SB_W, len, C_G0 + 6);
        vline(x, y, len, C_SHAD);
        panel(x + 1, y + tp, SB_W - 1, tl, 0);
    }
}

int sbar_from_pos(int len, int total, int vis, int pos)
{
    if (total <= vis) return 0;
    int tl, tp;
    thumb_geo(len, total, vis, 0, &tl, &tp);
    int span = len - tl;
    if (span <= 0) return 0;
    int off = (pos - tl / 2) * (total - vis) / span;
    if (off < 0) off = 0;
    if (off > total - vis) off = total - vis;
    return off;
}

void focus_rect(int x, int y, int w, int h)
{
    for (int i = 0; i < w; i += 2) {
        fill_rect(x + i, y, 1, 1, C_BLACK);
        fill_rect(x + i, y + h - 1, 1, 1, C_BLACK);
    }
    for (int j = 0; j < h; j += 2) {
        fill_rect(x, y + j, 1, 1, C_BLACK);
        fill_rect(x + w - 1, y + j, 1, 1, C_BLACK);
    }
}

void blit(int x, int y, int w, int h, const u8 *src, int spitch)
{
    int sx = 0, sy = 0;
    if (x < clx0) { sx += clx0 - x; w -= clx0 - x; x = clx0; }
    if (y < cly0) { sy += cly0 - y; h -= cly0 - y; y = cly0; }
    if (x + w > clx1) w = clx1 - x;
    if (y + h > cly1) h = cly1 - y;
    if (x < 0) { sx -= x; w += x; x = 0; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > SW) w = SW - x;
    if (y + h > SH) h = SH - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
        memcpy(BACKBUF + (y + j) * SW + x, src + (sy + j) * spitch + sx, w);
}

void blit_key(int x, int y, int w, int h, const u8 *src, int spitch, u8 key)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            u8 c = src[j * spitch + i];
            if (c != key) pixel(x + i, y + j, c);
        }
}

void read_rect(int x, int y, int w, int h, u8 *dst, int dpitch)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            dst[j * dpitch + i] = getpixel(x + i, y + j);
}

void palette_set(int idx, u8 r, u8 g, u8 b)
{
    dac((u8)idx, r >> 2, g >> 2, b >> 2);
}

static u32 bmp_u32(const u8 *d) { return d[0] | (d[1] << 8) | (d[2] << 16) | ((u32)d[3] << 24); }

int bmp_load(const u8 *bm, u32 n, u8 *out, int outcap, int *w, int *h)
{
    if (n < 54 || bm[0] != 'B' || bm[1] != 'M') return -1;
    u32 off = bmp_u32(bm + 10);
    int iw = (int)bmp_u32(bm + 18);
    int ih = (int)bmp_u32(bm + 22);
    u16 bpp = bm[28] | (bm[29] << 8);
    if (bmp_u32(bm + 30) != 0) return -1;
    int topdown = ih < 0; if (topdown) ih = -ih;
    if (iw <= 0 || ih <= 0 || (bpp != 8 && bpp != 24)) return -1;
    if (iw * ih > outcap) return -1;

    u8 map[256];
    if (bpp == 8) {
        u32 paloff = 14 + bmp_u32(bm + 14);
        for (int i = 0; i < 256; i++) {
            const u8 *e = bm + paloff + i * 4;
            map[i] = (paloff + (u32)(i + 1) * 4 <= n)
                     ? palette_nearest(e[2], e[1], e[0]) : (u8)i;
        }
    }
    int rowsz = bpp == 8 ? ((iw + 3) & ~3) : ((iw * 3 + 3) & ~3);
    for (int y = 0; y < ih; y++) {
        int src = topdown ? y : (ih - 1 - y);
        u8 *drow = out + (u32)y * iw;
        if (off + (u32)(src + 1) * rowsz > n) { memset(drow, C_WHITE, iw); continue; }
        const u8 *row = bm + off + (u32)src * rowsz;
        for (int x = 0; x < iw; x++)
            drow[x] = bpp == 8 ? map[row[x]]
                    : palette_nearest(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
    }
    *w = iw; *h = ih;
    return iw * ih;
}

int text_width(const char *s) { return (int)strlen(s) * 8; }
int font_height(void) { return 16; }
int text_fit(const char *s, int maxpx)
{
    int cells = maxpx / 8, len = (int)strlen(s);
    return len < cells ? len : (cells < 0 ? 0 : cells);
}

static u8 cur_hidden;
static int cur_kind;
void cursor_hide(int hide) { cur_hidden = hide ? 1 : 0; }
void cursor_shape(int s)   { cur_kind = s; }

static const char *const cur_arrow[] = {
    "X..........", "XX.........", "XoX........", "XooX.......",
    "XoooX......", "XooooX.....", "XoooooX....", "XooooooX...",
    "XoooooooX..", "XooooooooX.", "XoooooXXXXX", "XooXooX....",
    "XoX.XooX...", "XX..XooX...", "X....XooX..", ".....XooX..",
    "......XX...",
};

void draw_cursor(int x, int y)
{
    if (cur_hidden) return;
    if (cur_kind == CUR_TEXT) {
        hline(x - 2, y, 5, C_BLACK); hline(x - 2, y + 15, 5, C_BLACK);
        vline(x, y, 16, C_BLACK);
        return;
    }
    if (cur_kind == CUR_CROSS) {
        hline(x - 6, y, 13, C_BLACK); vline(x, y - 6, 13, C_BLACK);
        return;
    }
    if (cur_kind == CUR_BUSY) {
        fill_rect(x, y, 11, 2, C_BLACK); fill_rect(x, y + 14, 11, 2, C_BLACK);
        for (int j = 0; j < 7; j++) { pixel(x + 1 + j, y + 2 + j, C_BLACK); pixel(x + 9 - j, y + 2 + j, C_BLACK); }
        for (int j = 0; j < 7; j++) { pixel(x + 4, y + 8 + j, C_BLACK); pixel(x + 5, y + 8 + j, C_BLACK); pixel(x + 6, y + 8 + j, C_BLACK); }
        return;
    }
    for (int j = 0; j < 17; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) continue;
        const char *row = cur_arrow[j];
        u8 *dst = BACKBUF + yy * SW;
        for (int i = 0; row[i]; i++) {
            if (row[i] == '.') continue;
            int xx = x + i;
            if (xx >= 0 && xx < SW)
                dst[xx] = row[i] == 'X' ? C_BLACK : C_WHITE;
        }
    }
}

void flip(void)
{

    if (!shadow_attempted && (BOOTINFO->vbe == 2 || (u32)lfb == 0xa0000) && heap_avail()) {
        shadow_attempted = 1;
        u32 bytes = (u32)SW * SH;
        if (heap_avail() > bytes + 131072u) flip_shadow = kmalloc(bytes);
    }
    for (int y = 0; y < SH; y++) {
        int x = 0, len = SW;
        if (flip_shadow && shadow_valid &&
            !fb_span(BACKBUF + y * SW, flip_shadow + y * SW, SW, &x, &len)) {
            fb_skip_bytes += SW;
            continue;
        }
        fb_copy_bytes += len;
        fb_skip_bytes += SW - len;
        const u8 *source = BACKBUF;
        if (flip_shadow) {
            memcpy(flip_shadow + y * SW + x, BACKBUF + y * SW + x, len);
            source = flip_shadow;
        }
        if (BOOTINFO->vbe != 2) {
            memcpy(lfb + y * SPITCH + x, source + y * SW + x, len);
        } else {
            VbPart p[2];
            int n = vblit_span(y, x, len, SW, SPITCH, p);
            for (int i = 0; i < n; i++) {
                if ((int)p[i].bank != vb_bank) vb_set_bank((int)p[i].bank);
                memcpy((u8 *)0xA0000 + p[i].win_off, source + p[i].src_off,
                       p[i].len);
            }
        }
    }
    if (flip_shadow) shadow_valid = 1;
}

static const char *exc_name[] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound range",
    "invalid opcode", "no FPU", "double fault", "FPU segment", "bad TSS",
    "segment not present", "stack fault", "general protection", "page fault",
    "reserved", "FPU error", "alignment", "machine check", "SIMD"
};

static __attribute__((minsize)) const char *panic_write_report(u32 vec, u32 err, u32 eip)
{
    if (!usb_present())            return "no USB device - report not saved";
    if (!fat_mount())              return "USB not mounted - report not saved";
    if (!fat_writable())           return "USB read-only - report not saved";

    static char rep[5200];
    static char log[2048], evt[4096];
    int ln = klog_read(log, sizeof log);   if (ln < 0) ln = 0; log[ln] = 0;
    int en = trace_read(evt, sizeof evt);  if (en < 0) en = 0; evt[en] = 0;
    const char *owner = kext_at(eip);
    const char *nm = vec < 20 ? exc_name[vec] : "exception";
    int n = panic_report_fmt(rep, sizeof rep, OS_RELEASE, vec, nm, err, eip,
                             owner, vec == 14, fault_cr2, fault_recoveries,
                             evt, log);

    const char *result = "report write faulted - not saved";
    usb_quiet = 1;
    FAULT_GUARD(
        result = (fat_write("PANIC.TXT", (const u8 *)rep, (u32)n) == 0)
                 ? "saved to USB:PANIC.TXT" : "USB write failed",
        result = "report write faulted - not saved");
    usb_quiet = 0;
    return result;
}

__attribute__((minsize)) void panic(u32 vec, u32 err, u32 eip)
{
    emergency_panic_begin(vec,err,eip);
    if (!SW) emergency_enter(vec,err,eip,vec==14 ? fault_cr2 : 0,EM_REPORT);
    surface_unlock();
    clear_clip();
    fill_rect(0, 0, SW, SH, C_NAVY);
    char buf[80];
    int x = SW > 640 ? (SW - 608) / 2 : 16;
    int width = SW - 2*x;
    int y = SH > 220 ? (SH - 190) / 2 : 8;
    draw_text(x, y, "FLOPNIX stopped", C_WHITE);
    hline(x, y + 20, width, C_G0 + 3);
    const char *nm = vec < 20 ? exc_name[vec] : "exception";
    kfmt(buf, sizeof buf, "P%u: %s", vec, nm);
    draw_text_clip(x, y + 28, buf, C_WHITE, width);
    kfmt(buf, sizeof buf, "Instruction: %08x", eip);
    draw_text(x, y + 44, buf, C_WHITE);
    const char *kx = kext_at(eip);
    kfmt(buf, sizeof buf, "Code owner: %s", kx ? kx : "kernel / unknown");
    draw_text_clip(x, y + 58, buf, C_WHITE, width);
    const KextInfo *active = kext_get(kext_current());
    kfmt(buf, sizeof buf, "Active context: %s", active ? active->name : "kernel");
    draw_text_clip(x, y + 72, buf, C_SILVER, width);
    kfmt(buf, sizeof buf, "Error bits: %08x", err);
    draw_text(x, y + 86, buf, C_WHITE);
    if (vec == 14) {
        kfmt(buf, sizeof buf, "Memory address: %08x", fault_cr2);
        draw_text(x, y + 100, buf, C_YELLOW);
        draw_text_clip(x, y + 114, panic_pf_reason(err), C_YELLOW, width);
    }
    draw_text_clip(x, y + 138, "Restart to continue. Save this screen", C_SILVER, width);
    draw_text(x, y + 152, "when reporting the problem.", C_SILVER);
    flip();
    cli();
    const char *rep = panic_write_report(vec, err, eip);
    draw_text_clip(x, y + 176, rep, C_G0 + 5, width);
    flip();

    cli();
    for (;;) hlt();
}
