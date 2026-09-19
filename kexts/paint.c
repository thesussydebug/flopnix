/* Edits an indexed image through a zoomable canvas. */
#include "kapi.h"
#include "shpath.h"
#include "paintview.inc"
#include "gdi.h"
#include "menushade.h"
#include "button.h"
#include "dynbuf.h"

static const Kapi *api;
static int paint_type = -1;

#define SW              (*api->screen_w)
#define SH              (*api->screen_h)
#define mx              (*api->mouse_x)
#define my              (*api->mouse_y)
#define fill_rect       api->fill_rect
#define hline           api->hline
#define vline           api->vline
#define panel           api->panel
#define bevel           api->bevel
#define draw_char       api->draw_char
#define draw_text       api->draw_text
#define draw_text_clip  api->draw_text_clip
#define text_width      api->text_width
#define font_glyph      api->font_glyph
#define blit            api->blit
#define palette_rgb     api->palette_rgb
#define palette_nearest api->palette_nearest
#define kfmt            api->kfmt
#define strlen          api->strlen
#define strcasecmp      api->strcasecmp
#define strlcpy         api->strlcpy
#define memcpy          api->memcpy
#define memset          api->memset
#define fat_list        api->fat_list
#define fat_read        api->fat_read
#define fat_write       api->fat_write
#define fat_writable    api->fat_writable
#define fs_write        api->fs_write
#define win_open        api->win_open
#define win_fit_client  api->win_fit_client
#define iobuf           api->iobuf
#define IOBUF_SZ        api->iobuf_size
#define WT_PAINT        paint_type

static int    pdefw, pdefh;

#define PCW_MAX 320
#define PCH_MAX 200
#define PAINT_INST 2
#define TOP_H 24
#define LEFT_W 80
#define SW_H 44
#define NTOOL 10
#define MINW 400
#define BMPHDR (54 + 1024)
enum { PM_NORM, PM_FILE, PM_EDIT, PM_VIEW, PM_SIZE };
enum { A_NEW, A_OPEN, A_SAVE, A_SAVEAS, A_UNDO, A_CLEAR, A_FLIPH, A_FLIPV,
       A_ZOOM1, A_ZOOM2, A_ZOOM4, A_SIZE };

enum { T_PEN, T_LINE, T_RECT, T_BOX, T_OVAL, T_DISC, T_FILL, T_TEXT,
       T_PICK, T_ERASE };

typedef struct {
    u8 *canvas;
    DynBuf storage;
    int  cw, ch;
    u8   col; int lx, ly;
    int  mode;
    char msg[64];
    int zoom, ox, oy, vw, vh;
    u8   has_file;
    u8   tool;
    u8   brush;
    int  ax, ay;
    int  bx, by;
    u8   dragging;
    int  tx, ty;
    char text[40];
    u8   fsrc;
    char fpath[96];
} Paint;
static Paint paints[PAINT_INST];
static u8 active[PAINT_INST];
static u8 *icon_cache;
static u8 icon_color;

static DynBuf undo_storage;
#define undo_buf ((u8 *)undo_storage.data)
static int undo_inst = -1, undo_w, undo_h;

static const struct { int w, h; } psizes[4] = {
    { 160, 100 }, { 240, 140 }, { 272, 150 }, { 320, 200 }
};
static const char *const tool_name[NTOOL] = {
    "Pencil", "Line", "Rectangle", "Solid box", "Ellipse", "Solid oval",
    "Fill", "Text", "Eyedropper", "Eraser"
};
static const char *const tool_help[NTOOL] = {
    "Drag to draw", "Drag between two points", "Drag an outline",
    "Drag a filled rectangle", "Drag an oval outline", "Drag a filled oval",
    "Click to fill an area", "Click, type, then press Enter",
    "Click to pick a colour", "Drag to erase"
};
static const char *const menus[3][4] = {
    {"New          Ctrl+N", "Open...      Ctrl+O", "Save         Ctrl+S", "Save as..."},
    {"Undo / redo  Ctrl+Z", "Clear picture", "Flip horizontally", "Flip vertically"},
    {"Actual size   100%", "Zoom in       200%", "Zoom in       400%", "Canvas size..."}
};
static void view_clamp(Paint *p)
{
    p->ox = pv_limit(p->cw, p->vw, p->zoom, p->ox);
    p->oy = pv_limit(p->ch, p->vh, p->zoom, p->oy);
}

static const char *pbase(const char *s)
{
    const char *b = s;
    for (const char *q = s; *q; q++)
        if (*q == '/' || *q == ':') b = q + 1;
    return b;
}

static void paint_reset(int inst)
{
    active[inst]=1;
    Paint *p = &paints[inst];
    if (!db_reserve(api, &p->storage, (u32)pdefw*pdefh, 1, 4096, PCW_MAX*PCH_MAX, "Paint canvas")) {
        p->cw=p->ch=0; p->zoom=1;
        strlcpy(p->msg,"Not enough memory. Close and reopen Paint.",sizeof p->msg); return;
    }
    p->canvas=p->storage.data;
    memset(p->canvas, C_WHITE, (u32)pdefw*pdefh);
    p->cw = pdefw;
    p->ch = pdefh;
    p->col = C_RED;
    p->lx = -1;
    p->zoom = 1; p->ox = p->oy = 0; p->vw = p->cw; p->vh = p->ch;
    p->mode = PM_NORM;
    p->msg[0] = 0;
    p->has_file = 0;
    p->fpath[0] = 0;
    p->tool = T_PEN;
    p->brush = 1;
    p->dragging = 0;
    p->tx = -1;
    p->text[0] = 0;
}

static int undo_snap(Paint *p)
{
    if (!db_reserve(api,&undo_storage,(u32)p->cw*p->ch,1,4096,PCW_MAX*PCH_MAX,"Paint undo")) {
        strlcpy(p->msg,"Not enough memory for undo. Picture unchanged.",sizeof p->msg); return 0;
    }
    memcpy(undo_buf, p->canvas, (u32)p->cw * p->ch);
    undo_inst = (int)(p - paints);
    undo_w = p->cw;
    undo_h = p->ch;
    return 1;
}

static int undo_swap(Paint *p)
{
    if (undo_inst != (int)(p - paints) || undo_w != p->cw || undo_h != p->ch)
        return 0;

    u32 n = (u32)p->cw * p->ch;
    for(u32 i=0;i<n;i++){u8 c=p->canvas[i];p->canvas[i]=undo_buf[i];undo_buf[i]=c;}
    return 1;
}

static void paint_set_size(Paint *p, int nw, int nh)
{
    if (nw == p->cw && nh == p->ch) return;
    if(nw<1||nh<1||nw>PCW_MAX||nh>PCH_MAX)return;
    if (!db_reserve(api,&p->storage,(u32)nw*nh,1,4096,PCW_MAX*PCH_MAX,"Paint canvas")) {
        strlcpy(p->msg,"Not enough memory to resize.",sizeof p->msg); return;
    }
    p->canvas=p->storage.data;
    int ow = p->cw, oh = p->ch;
    int kw=nw<ow?nw:ow,kh=nh<oh?nh:oh;
    for(int i=0;i<kh;i++){
        int y=nw>ow?kh-1-i:i;
        api->memmove(p->canvas+y*nw,p->canvas+y*ow,kw);
        if(nw>kw)memset(p->canvas+y*nw+kw,C_WHITE,nw-kw);
    }
    if(nh>kh)memset(p->canvas+kh*nw,C_WHITE,(nh-kh)*nw);
    p->cw = nw;
    p->ch = nh;
    db_trim(api,&p->storage,(u32)nw*nh,1);p->canvas=p->storage.data;
    undo_inst = -1;
}

typedef void (*PlotFn)(void *ctx, int x, int y, u8 col);

static void plot_canvas(void *ctx, int x, int y, u8 col)
{
    Paint *p = (Paint *)ctx;
    if (x >= 0 && x < p->cw && y >= 0 && y < p->ch)
        p->canvas[y * p->cw + x] = col;
}

typedef struct { int ox, oy, w, h, sx, sy, zoom; } ScreenCtx;

static void plot_screen(void *ctx, int x, int y, u8 col)
{
    ScreenCtx *s = (ScreenCtx *)ctx;
    x = (x - s->sx) * s->zoom; y = (y - s->sy) * s->zoom;
    if (x >= 0 && x < s->w && y >= 0 && y < s->h) {
        int w = s->w - x, h = s->h - y;
        if (w > s->zoom) w = s->zoom;
        if (h > s->zoom) h = s->zoom;
        fill_rect(s->ox + x, s->oy + y, w, h, col);
    }
}

static void dab(PlotFn plot, void *ctx, int x, int y, u8 col, int size)
{
    int r = size / 2;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) plot(ctx, x + i, y + j, col);
}

static void ras_line(PlotFn plot, void *ctx, int x0, int y0, int x1, int y1,
                     u8 col, int size)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        dab(plot, ctx, x0, y0, col, size);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void norm_box(int *x0, int *y0, int *x1, int *y1)
{
    if (*x1 < *x0) { int t = *x0; *x0 = *x1; *x1 = t; }
    if (*y1 < *y0) { int t = *y0; *y0 = *y1; *y1 = t; }
}

static void ras_rect(PlotFn plot, void *ctx, int x0, int y0, int x1, int y1,
                     u8 col, int size)
{
    norm_box(&x0, &y0, &x1, &y1);
    ras_line(plot, ctx, x0, y0, x1, y0, col, size);
    ras_line(plot, ctx, x0, y1, x1, y1, col, size);
    ras_line(plot, ctx, x0, y0, x0, y1, col, size);
    ras_line(plot, ctx, x1, y0, x1, y1, col, size);
}

static void ras_box(PlotFn plot, void *ctx, int x0, int y0, int x1, int y1, u8 col)
{
    norm_box(&x0, &y0, &x1, &y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) plot(ctx, x, y, col);
}

static int ell_halfw(int rx, int ry, int dy)
{
    if (!ry) return rx;
    int t = ry * ry - dy * dy;
    if (t <= 0) return 0;
    return (int)isqrt((u32)(rx * rx * t / (ry * ry)));
}

static void ras_oval(PlotFn plot, void *ctx, int x0, int y0, int x1, int y1,
                     u8 col, int size)
{
    norm_box(&x0, &y0, &x1, &y1);
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    int prev = -1;
    for (int dy = -ry; dy <= ry; dy++) {
        int w = ell_halfw(rx, ry, dy);
        dab(plot, ctx, cx - w, cy + dy, col, size);
        dab(plot, ctx, cx + w, cy + dy, col, size);

        if (prev >= 0) {
            int lo = w < prev ? w : prev, hi = w < prev ? prev : w;
            for (int x = lo + 1; x < hi; x++) {
                dab(plot, ctx, cx - x, cy + dy, col, size);
                dab(plot, ctx, cx + x, cy + dy, col, size);
            }
        }
        prev = w;
    }
}

static void ras_disc(PlotFn plot, void *ctx, int x0, int y0, int x1, int y1, u8 col)
{
    norm_box(&x0, &y0, &x1, &y1);
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int rx = (x1 - x0) / 2, ry = (y1 - y0) / 2;
    for (int dy = -ry; dy <= ry; dy++) {
        int w = ell_halfw(rx, ry, dy);
        for (int x = cx - w; x <= cx + w; x++) plot(ctx, x, cy + dy, col);
    }
}

#define FILL_STACK 2048
static void ras_fill(Paint *p, int sx, int sy, u8 col)
{
    if (sx < 0 || sx >= p->cw || sy < 0 || sy >= p->ch) return;
    u8 target = p->canvas[sy * p->cw + sx];
    if (target == col) return;
    short (*stk)[2]=api->kmalloc(FILL_STACK*sizeof *stk);
    if(!stk){strlcpy(p->msg,"Not enough memory to fill.",sizeof p->msg);return;}
    api->mem_track("Paint fill workspace",stk,FILL_STACK*sizeof *stk);
    int sp = 0;
    stk[sp][0] = (short)sx; stk[sp][1] = (short)sy; sp++;
    while (sp > 0) {
        sp--;
        int x = stk[sp][0], y = stk[sp][1];
        u8 *row = p->canvas + y * p->cw;
        if (row[x] != target) continue;
        int l = x, r = x;
        while (l > 0 && row[l - 1] == target) l--;
        while (r < p->cw - 1 && row[r + 1] == target) r++;
        for (int i = l; i <= r; i++) row[i] = col;

        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = y + dy;
            if (ny < 0 || ny >= p->ch) continue;
            u8 *nr = p->canvas + ny * p->cw;
            int i = l;
            while (i <= r) {
                while (i <= r && nr[i] != target) i++;
                if (i > r) break;
                if (sp < FILL_STACK) {
                    stk[sp][0] = (short)i; stk[sp][1] = (short)ny; sp++;
                }
                while (i <= r && nr[i] == target) i++;
            }
        }
    }
    api->kfree(stk);
}

static void ras_text(Paint *p, int x, int y, const char *s, u8 col)
{
    for (int i = 0; s[i]; i++) {
        const u8 *gl = font_glyph(s[i]);
        for (int j = 0; j < 16; j++) {
            u8 bits = gl[j];
            if (!bits) continue;
            for (int b = 0; b < 8; b++)
                if (bits & (0x80 >> b)) plot_canvas(p, x + i * 8 + b, y + j, col);
        }
    }
}

static void put32(u8 *d, u32 v) { d[0]=v; d[1]=v>>8; d[2]=v>>16; d[3]=v>>24; }
static void put16(u8 *d, u16 v) { d[0]=v; d[1]=v>>8; }
static u32  get32(const u8 *d) { return d[0]|(d[1]<<8)|(d[2]<<16)|((u32)d[3]<<24); }
static u16  get16(const u8 *d) { return d[0]|(d[1]<<8); }

static u32 paint_build_bmp(Paint *p)
{
    int rowsz = (p->cw + 3) & ~3;
    u32 imgsz = (u32)rowsz * p->ch;
    u32 fsz = BMPHDR + imgsz;
    memset(iobuf, 0, fsz);
    iobuf[0] = 'B'; iobuf[1] = 'M';
    put32(iobuf + 2, fsz);
    put32(iobuf + 10, BMPHDR);
    put32(iobuf + 14, 40);
    put32(iobuf + 18, p->cw);
    put32(iobuf + 22, p->ch);
    put16(iobuf + 26, 1);
    put16(iobuf + 28, 8);
    put32(iobuf + 34, imgsz);
    put32(iobuf + 38, 2835); put32(iobuf + 42, 2835);
    put32(iobuf + 46, 256);
    for (int i = 0; i < 256; i++) {
        u8 r, gg, b;
        palette_rgb(i, &r, &gg, &b);
        iobuf[54 + i * 4] = b; iobuf[54 + i * 4 + 1] = gg;
        iobuf[54 + i * 4 + 2] = r; iobuf[54 + i * 4 + 3] = 0;
    }
    for (int f = 0; f < p->ch; f++) {
        u8 *dst = iobuf + BMPHDR + (u32)f * rowsz;
        int img = p->ch - 1 - f;
        memcpy(dst, p->canvas + img * p->cw, p->cw);
    }
    return fsz;
}

static int paint_write_to_locked(Paint *p, int src, const char *path)
{
    u32 fsz = paint_build_bmp(p);
    return src == 1 ? fat_write(path, iobuf, fsz) : fs_write(path, iobuf, fsz);
}
static int paint_write_to(Paint *p,int src,const char *path){api->buffer_lock();int result=paint_write_to_locked(p,src,path);api->buffer_unlock();return result;}


static int paint_save_to(Paint *p, int drive, const char *path)
{
    if (drive == 1 && !fat_writable()) {
        strlcpy(p->msg, "USB read-only", sizeof p->msg);
        return -1;
    }
    int r = paint_write_to(p, drive, path);
    if (r == 0) kfmt(p->msg, sizeof p->msg, "saved %s", pbase(path));
    else if (r == -2) strlcpy(p->msg, "disk full", sizeof p->msg);
    else if (r == -3) strlcpy(p->msg, "that name is a folder", sizeof p->msg);
    else strlcpy(p->msg, "save failed", sizeof p->msg);
    return r;
}
static void paint_save_current(Paint *p)
{
    paint_save_to(p, p->fsrc, p->fpath);
}

static int paint_parse_bmp(Paint *p, const u8 *bm, int n)
{
    if (n < 54 || bm[0] != 'B' || bm[1] != 'M') return -1;
    u32 off = get32(bm + 10);
    int w = (int)get32(bm + 18);
    int h = (int)get32(bm + 22);
    u16 bpp = get16(bm + 28);
    u32 dib=get32(bm+14);
    if(dib<40||dib>(u32)n-14||get16(bm+26)!=1||off<14+dib||off>(u32)n)return -1;
    if (get32(bm + 30) != 0 || h == (-2147483647-1)) return -1;
    int topdown = h < 0; if (topdown) h = -h;
    if (w <= 0 || h <= 0 || (bpp != 8 && bpp != 24)) return -1;
    u32 pixel_bytes=bpp/8;
    if((u32)w>(u32)n/pixel_bytes)return -1;
    u32 rowsz=((u32)w*pixel_bytes+3)&~3u;
    if((u32)h>((u32)n-off)/rowsz)return -1;

    u8 map[256];
    if (bpp == 8) {
        u32 paloff = 14 + dib,colors=get32(bm+46);
        if(!colors)colors=256;
        if(colors>256||colors>(off-paloff)/4)return -1;
        memset(map,C_BLACK,sizeof map);
        for (u32 i = 0; i < colors; i++) {
            const u8 *e = bm + paloff + i * 4;
            map[i] = palette_nearest(e[2], e[1], e[0]);
        }
    }
    int nw=w < 16 ? 16 : (w > PCW_MAX ? PCW_MAX : w);
    int nh=h < 16 ? 16 : (h > PCH_MAX ? PCH_MAX : h);
    if(!db_reserve(api,&p->storage,(u32)nw*nh,1,4096,PCW_MAX*PCH_MAX,"Paint canvas"))return -1;
    p->canvas=p->storage.data;p->cw=nw;p->ch=nh;
    memset(p->canvas, C_WHITE, (u32)p->cw*p->ch);
    for (int y = 0; y < p->ch && y < h; y++) {
        int src = topdown ? y : (h - 1 - y);
        if (off + (u32)(src + 1) * rowsz > (u32)n) continue;
        const u8 *row = bm + off + (u32)src * rowsz;
        for (int x = 0; x < p->cw && x < w; x++) {
            if (bpp == 8) p->canvas[y * p->cw + x] = map[row[x]];
            else { const u8 *px = row + x * 3; p->canvas[y * p->cw + x] = palette_nearest(px[2], px[1], px[0]); }
        }
    }
    undo_inst = -1;
    return 0;
}

static int paint_fit_w(Paint *p);
static int paint_fit_h(Paint *p);

static void paint_bind(Paint *p, const char *spec)
{
    int drive;
    char path[96];
    sh_spec_split(spec, &drive, path, sizeof path);
    p->fsrc = (u8)drive;
    strlcpy(p->fpath, path, sizeof p->fpath);
    p->has_file = 1;
}

static int paint_load_spec_locked(Paint *p, const char *spec)
{
    int drive;
    char path[96];
    sh_spec_split(spec, &drive, path, sizeof path);
    int n = drive == 1 ? fat_read(path, iobuf, IOBUF_SZ)
                       : api->fs_read(path, iobuf, IOBUF_SZ);
    if (paint_parse_bmp(p, iobuf, n) != 0) return -1;
    paint_bind(p, spec);
    return 0;
}
static int paint_load_spec(Paint *p,const char *spec){api->buffer_lock();int result=paint_load_spec_locked(p,spec);api->buffer_unlock();return result;}


static void paint_opened(const char *spec, void *ctx)
{
    Paint *p = (Paint *)ctx;
    if (!spec) return;
    if (paint_load_spec(p, spec) == 0)
        kfmt(p->msg, sizeof p->msg, "opened %dx%d", p->cw, p->ch);
    else strlcpy(p->msg, "open failed", sizeof p->msg);
    win_fit_client(WT_PAINT, (int)(p - paints), paint_fit_w(p), paint_fit_h(p));
    api->gui_dirty();
}

static void paint_saved_as(const char *spec, void *ctx)
{
    Paint *p = (Paint *)ctx;
    if (!spec) return;
    int drive;
    char path[132];
    sh_spec_split(spec, &drive, path, sizeof path);
    if (strlen(path) >= sizeof p->fpath)
        strlcpy(p->msg, "save path too long", sizeof p->msg);
    else if (paint_save_to(p, drive, path) == 0)
        paint_bind(p, spec);
    api->gui_dirty();
}

static void text_commit(Paint *p, int keep)
{
    if (p->tx >= 0 && p->text[0]) {
        if(!undo_snap(p))return;
        ras_text(p, p->tx, p->ty, p->text, p->col);
    }
    p->text[0] = 0;
    if (!keep) p->tx = -1;
}

static void paint_action(Paint *p, int action)
{
    text_commit(p, 0);
    p->dragging = 0; p->lx = -1; p->mode = PM_NORM; p->msg[0] = 0;
    switch (action) {
    case A_NEW:
        if(!undo_snap(p))return;
        memset(p->canvas, C_WHITE, (u32)p->cw*p->ch);
        p->has_file = 0; p->fpath[0] = 0; break;
    case A_OPEN:
        api->file_picker("Open picture", "bmp", 0, paint_opened, p); break;
    case A_SAVE:
        if (p->has_file) { paint_save_current(p); break; }

    case A_SAVEAS:
        api->file_save("Save picture as", "bmp", p->has_file ? pbase(p->fpath) : "untitled",
                       paint_saved_as, p); break;
    case A_UNDO:
        strlcpy(p->msg, undo_swap(p) ? "Undo / redo" : "Nothing to undo", sizeof p->msg); break;
    case A_CLEAR:
        if(!undo_snap(p))return; memset(p->canvas, C_WHITE, (u32)p->cw*p->ch);
        strlcpy(p->msg, "Picture cleared. Ctrl+Z to undo.", sizeof p->msg); break;
    case A_FLIPH: case A_FLIPV:
        if(!undo_snap(p))return; pv_flip(p->canvas, p->cw, p->ch, action == A_FLIPV);
        strlcpy(p->msg, "Picture flipped. Ctrl+Z to undo.", sizeof p->msg); break;
    case A_ZOOM1: case A_ZOOM2: case A_ZOOM4:
        p->zoom = 1 << (action - A_ZOOM1); view_clamp(p);
        strlcpy(p->msg, "Arrows or mouse wheel to pan", sizeof p->msg); break;
    case A_SIZE: p->mode = PM_SIZE; break;
    }
    api->gui_dirty();
}

static void paint_key(int inst, int k)
{
    Paint *p = &paints[inst];
    if(!p->canvas)return;
    if (p->mode != PM_NORM) { if (k == 27) p->mode = PM_NORM; return; }
    if (k == 19) { paint_action(p, A_SAVE); return; }
    if (k == 15) { paint_action(p, A_OPEN); return; }
    if (k == 14) { paint_action(p, A_NEW); return; }
    if (p->tx >= 0) {
        if (k == '\n') text_commit(p, 0);
        else if (k == 27) { p->text[0] = 0; p->tx = -1; }
        else if (k == '\b') {
            int n = (int)strlen(p->text); if (n) p->text[n - 1] = 0;
        } else if (k >= 32 && k < 127) {
            int n = (int)strlen(p->text);
            if (n < (int)sizeof p->text - 1 && p->tx + (n + 1) * 8 <= p->cw) {
                p->text[n] = (char)k; p->text[n + 1] = 0;
            }
        }
    } else if (k == 26 || k == 'z' || k == 'Z') paint_action(p, A_UNDO);
    else if (k == '+' || k == '=') paint_action(p, p->zoom == 1 ? A_ZOOM2 : A_ZOOM4);
    else if (k == '-') paint_action(p, p->zoom == 4 ? A_ZOOM2 : A_ZOOM1);
    else if (!p->dragging && p->lx < 0) {
        if (k == K_LEFT) p->ox -= 16;
        if (k == K_RIGHT) p->ox += 16;
        if (k == K_UP) p->oy -= 16;
        if (k == K_DOWN) p->oy += 16;
        view_clamp(p);
    }
    api->gui_dirty();
}

static void paint_wheel(int inst, int dz)
{
    Paint *p = &paints[inst];
    if(!p->canvas)return;
    if (p->mode != PM_NORM || p->dragging || p->lx >= 0) return;
    if (api->kbd_mods() & 1) p->ox -= dz * 8;
    else p->oy -= dz * 8;
    view_clamp(p);
}

static int paint_fit_w(Paint *p)
{
    int w = LEFT_W + p->cw + 4; return w < MINW ? MINW : w;
}
static int paint_fit_h(Paint *p)
{
    int h = p->ch < 200 ? 200 : p->ch; return TOP_H + 8 + h + SW_H;
}

static void tool_select(Paint *p, int t)
{
    text_commit(p, 0); p->tool = (u8)t; p->dragging = 0; p->lx = -1;
    p->msg[0] = 0;
}

static void paint_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    Paint *p = &paints[inst];
    if(!p->canvas)return;
    p->vw = cw - LEFT_W - 4; p->vh = ch - TOP_H - SW_H - 8;
    view_clamp(p);
    int active = p->dragging || p->lx >= 0;

    if (!active || ev == EV_PRESS) {
        if (ly >= 0 && ly < TOP_H && lx >= 0 && lx < 144) {
            if (ev == EV_PRESS) {
                int m = PM_FILE + lx / 48;
                p->mode = p->mode == m ? PM_NORM : m;
            }
            return;
        }
        if (p->mode != PM_NORM) {
            if (ev != EV_PRESS) return;
            int mode = p->mode;
            int x0 = mode == PM_SIZE ? 96 : (mode - PM_FILE) * 48;
            p->mode = PM_NORM;
            if (lx < x0 || lx >= x0 + 224 || ly < TOP_H + 2 || ly >= TOP_H + 82) return;
            int row = (ly - TOP_H - 2) / 20;
            if (mode == PM_SIZE) {
                text_commit(p, 0);
                paint_set_size(p, psizes[row].w, psizes[row].h);
                p->ox = p->oy = 0; view_clamp(p);
                win_fit_client(WT_PAINT, inst, paint_fit_w(p), paint_fit_h(p));
                kfmt(p->msg, sizeof p->msg, "Canvas: %d x %d pixels", p->cw, p->ch);
            } else paint_action(p, (mode - PM_FILE) * 4 + row);
            return;
        }
        if (lx >= 4 && lx < 76 && ly >= TOP_H + 4 && ly < TOP_H + 164) {
            if (ev == EV_PRESS) tool_select(p, ((ly - TOP_H - 4) / 32) * 2 + (lx - 4) / 36);
            return;
        }
        if (lx >= 4 && lx < 76 && ly >= TOP_H + 182 && ly < TOP_H + 200) {
            if (ev == EV_PRESS) p->brush = (u8)(1 + 2 * ((lx - 4) / 24));
            return;
        }
        if (ly >= ch - SW_H && ly < ch - 20) {
            if (ev == EV_PRESS && lx >= 40) {
                int sw = (cw - 48) / 16, idx = (lx - 40) / sw;
                if (idx >= 0 && idx < 16) p->col = (u8)idx;
            }
            return;
        }
        if (lx < LEFT_W || lx >= LEFT_W + p->vw || ly < TOP_H + 4 || ly >= TOP_H + 4 + p->vh) return;
    }
    int px = lx - LEFT_W, py = ly - TOP_H - 4;
    if (ev == EV_PRESS && (px >= (p->cw - p->ox) * p->zoom || py >= (p->ch - p->oy) * p->zoom)) return;
    if (px >= p->vw) px = p->vw - 1;
    if (py >= p->vh) py = p->vh - 1;
    int qx = pv_point(px, p->zoom, p->ox, p->cw);
    int qy = pv_point(py, p->zoom, p->oy, p->ch);
    if (ev == EV_PRESS) p->msg[0] = 0;

    switch (p->tool) {
    case T_PICK:
        if (ev == EV_PRESS) {
            p->col = p->canvas[qy * p->cw + qx];
            kfmt(p->msg, sizeof p->msg, "picked colour %d", p->col);
        }
        return;
    case T_FILL:
        if (ev == EV_PRESS) { if(!undo_snap(p))return; ras_fill(p, qx, qy, p->col); }
        return;
    case T_TEXT:
        if (ev == EV_PRESS) {
            text_commit(p, 0);
            p->tx = qx;
            p->ty = qy;
            p->text[0] = 0;
            strlcpy(p->msg, "type, Enter stamps, Esc drops", sizeof p->msg);
        }
        return;
    case T_PEN:
    case T_ERASE: {
        u8 col = p->tool == T_ERASE ? C_WHITE : p->col;
        if (ev == EV_PRESS) {
            if(!undo_snap(p))return;
            p->lx = qx; p->ly = qy;
            dab(plot_canvas, p, qx, qy, col, p->brush);
        } else if (ev == EV_DRAG && p->lx >= 0) {
            ras_line(plot_canvas, p, p->lx, p->ly, qx, qy, col, p->brush);
            p->lx = qx; p->ly = qy;
        } else if (ev == EV_RELEASE) p->lx = -1;
        return;
    }
    default: break;
    }

    if (ev == EV_PRESS) {
        p->ax = qx; p->ay = qy;
        p->bx = qx; p->by = qy;
        p->dragging = 1;
    } else if (ev == EV_DRAG && p->dragging) {
        p->bx = qx; p->by = qy;
        api->gui_dirty();
    } else if (ev == EV_RELEASE && p->dragging) {
        p->dragging=0;
        if(!undo_snap(p))return;
        int x0 = p->ax, y0 = p->ay, x1 = qx, y1 = qy;
        switch (p->tool) {
        case T_LINE: ras_line(plot_canvas, p, x0, y0, x1, y1, p->col, p->brush); break;
        case T_RECT: ras_rect(plot_canvas, p, x0, y0, x1, y1, p->col, p->brush); break;
        case T_BOX:  ras_box(plot_canvas, p, x0, y0, x1, y1, p->col); break;
        case T_OVAL: ras_oval(plot_canvas, p, x0, y0, x1, y1, p->col, p->brush); break;
        case T_DISC: ras_disc(plot_canvas, p, x0, y0, x1, y1, p->col); break;
        }
        p->dragging = 0;
    }
}

static void tool_render(int t,PlotFn plot,void *ctx,u8 col)
{

    static const u8 pencil[][4] = {
        {5,20,7,14}, {7,14,18,3}, {18,3,23,8}, {23,8,12,19},
        {12,19,5,20}, {7,14,12,19}, {16,5,21,10}, {10,15,18,7},
        {5,20,7,18}, {6,20,7,19}
    };
    static const u8 bucket[][4] = {
        {5,11,11,5}, {5,11,14,20}, {14,20,20,14},
        {11,5,20,14}, {12,4,21,13}, {11,5,12,4}, {20,14,21,13},

        {9,7,9,3}, {9,3,11,1}, {11,1,14,1}, {14,1,16,3}, {16,3,16,8},

        {21,13,23,14}, {23,14,23,16}, {23,18,21,21}, {23,18,25,21},
        {21,21,25,21}, {22,22,24,22}, {23,19,23,21}, {22,20,24,20}
    };
    static const u8 dropper[][4] = {

        {18,2,21,2}, {21,2,24,5}, {24,5,24,8}, {24,8,21,11},
        {18,2,15,5}, {15,5,21,11}, {14,6,20,12},
        {13,7,7,13}, {7,13,7,16}, {7,16,4,19}, {4,19,4,21},
        {4,21,6,21}, {6,21,9,18}, {9,18,12,18}, {12,18,18,12},
        {10,15,15,10}
    };
    static const u8 eraser[][4] = {
        {4,15,14,5}, {14,5,23,5}, {23,5,26,8}, {26,8,16,18},
        {16,18,7,18}, {7,18,4,15}, {4,15,13,15}, {13,15,23,5},
        {13,15,16,18}, {9,10,18,10}, {18,10,21,13}, {5,21,24,21}
    };
    const u8 (*strokes)[4] = 0;
    unsigned count = 0;
    switch (t) {
    case T_PEN: strokes=pencil; count=sizeof pencil / sizeof pencil[0]; break;
    case T_FILL: strokes=bucket; count=sizeof bucket / sizeof bucket[0]; break;
    case T_PICK: strokes=dropper; count=sizeof dropper / sizeof dropper[0]; break;
    case T_ERASE: strokes=eraser; count=sizeof eraser / sizeof eraser[0]; break;
    case T_LINE: ras_line(plot, ctx, 5,19,23,4,col,1); break;
    case T_RECT: case T_BOX:
        if (t == T_RECT) ras_rect(plot,ctx,5,5,22,18,col,1);
        else ras_box(plot,ctx,5,5,22,18,col);
        break;
    case T_OVAL: case T_DISC:
        if (t == T_OVAL) ras_oval(plot,ctx,5,5,22,18,col,1);
        else ras_disc(plot,ctx,5,5,22,18,col);
        break;
    case T_TEXT:
        ras_line(plot,ctx,7,20,13,4,col,1);
        ras_line(plot,ctx,13,4,15,4,col,1);
        ras_line(plot,ctx,15,4,21,20,col,2);
        ras_line(plot,ctx,10,14,18,14,col,1);
        ras_line(plot,ctx,5,20,10,20,col,1);
        ras_line(plot,ctx,18,20,24,20,col,1);
        break;
    }
    for (unsigned i=0; i<count; i++)
        ras_line(plot,ctx,strokes[i][0],strokes[i][1],strokes[i][2],strokes[i][3],col,1);
}

static void icon_plot(void *ctx,int x,int y,u8 col)
{
    if(x>=0&&x<28&&y>=0&&y<24)((u8 *)ctx)[y*28+x]=col;
}
static void tool_icon(int t,int x,int y,u8 col)
{
    if(!icon_cache&&api->mem_info(MI_HEAP_FREE)>32768){
        icon_cache=api->kmalloc(2*NTOOL*28*24);
        if(icon_cache){
            memset(icon_cache,255,2*NTOOL*28*24);
            for(int i=0;i<NTOOL;i++)tool_render(i,icon_plot,icon_cache+i*28*24,C_BLACK);
            icon_color=255;
            if(api->mem_track)api->mem_track("Paint tool icons",icon_cache,2*NTOOL*28*24);
        }
    }
    if(icon_cache){
        if(col!=C_BLACK&&icon_color!=col){
            memset(icon_cache+NTOOL*28*24,255,NTOOL*28*24);
            for(int i=0;i<NTOOL;i++)tool_render(i,icon_plot,icon_cache+(NTOOL+i)*28*24,col);
            icon_color=col;
        }
        api->blit_key(x,y,28,24,icon_cache+((col!=C_BLACK?NTOOL:0)+t)*28*24,28,255);
    }
    else{ScreenCtx sc={x,y,28,24,0,0,1};tool_render(t,plot_screen,&sc,col);}
}
static void paint_close(int inst)
{
    active[inst]=0;
    db_free(api,&paints[inst].storage);paints[inst].canvas=0;
    if(undo_inst==inst){db_free(api,&undo_storage);undo_inst=-1;}
    for(int i=0;i<PAINT_INST;i++)if(active[i])return;
    if(icon_cache){api->kfree(icon_cache);icon_cache=0;}
    db_free(api,&undo_storage);undo_inst=-1;
}

static void paint_draw(Win *w, int cx, int cy, int cw, int ch)
{
    Paint *p = &paints[w->inst];
    if(!p->canvas){draw_text_clip(cx+8,cy+12,p->msg,C_RED,cw-16);return;}
    p->vw = cw - LEFT_W - 4; p->vh = ch - TOP_H - SW_H - 8;
    view_clamp(p);
    fill_rect(cx, cy, cw, ch, C_FACE);
    menu_shade(api,cx,cy,cw,TOP_H-1,0);
    hline(cx, cy + TOP_H - 1, cw, C_SHAD);
    static const char *const labels[3] = { "File", "Edit", "View" };
    for (int i = 0; i < 3; i++) {
        int on = p->mode == PM_FILE + i || (i == 2 && p->mode == PM_SIZE);
        if(on||api->control_state(cx+i*48,cy+2,48,20))menu_shade(api,cx+i*48,cy+2,48,20,1);
        draw_text(cx + i * 48 + 8, cy + 4, labels[i], (on||api->control_state(cx+i*48,cy+2,48,20)) ? C_WHITE : C_BLACK);
    }
    draw_text_clip(cx + 154, cy + 4, p->has_file ? pbase(p->fpath) : "Untitled.bmp", C_DARK, cw - 160);
    int hover = -1;
    for (int t = 0; t < NTOOL; t++) {
        int x = cx + 4 + t % 2 * 36, y = cy + TOP_H + 4 + t / 2 * 32;
        int state=button_face(api,x,y,34,30,p->tool==t,1);
        tool_icon(t,x+3+(state==2),y+3+(state==2),state==2?C_WHITE:C_BLACK);
        if (mx >= x && mx < x + 34 && my >= y && my < y + 30) hover = t;
    }
    draw_text(cx + 12, cy + TOP_H + 166, "Brush", C_DARK);
    for (int i = 0; i < 3; i++) {
        int x = cx + 4 + i * 24, y = cy + TOP_H + 182, d = 1 + 2 * i;
        int state=button_face(api,x,y,22,18,p->brush==d,1);
        fill_rect(x+11-d/2,y+9-d/2,d,d,state==2?C_WHITE:C_BLACK);
    }

    int ox = cx + LEFT_W, oy = cy + TOP_H + 4;
    fill_rect(ox - 2, oy - 2, p->vw + 4, p->vh + 4, C_SHAD);
    fill_rect(ox, oy, p->vw, p->vh, C_G0 + 2);
    int bw = (p->cw - p->ox) * p->zoom, bh = (p->ch - p->oy) * p->zoom;
    if (bw > p->vw) bw = p->vw;
    if (bh > p->vh) bh = p->vh;
    if (p->zoom == 1) blit(ox, oy, bw, bh, p->canvas + p->oy * p->cw + p->ox, p->cw);
    else {

        for (int y = 0; y < bh; y += p->zoom) {
            int dh = bh - y; if (dh > p->zoom) dh = p->zoom;
            const u8 *row = p->canvas + (p->oy + y / p->zoom) * p->cw + p->ox;
            for (int x = 0; x < bw;) {
                int end = x + p->zoom;
                u8 col = row[x / p->zoom];
                while (end < bw && row[end / p->zoom] == col) end += p->zoom;
                if (end > bw) end = bw;
                fill_rect(ox + x, oy + y, end - x, dh, col); x = end;
            }
        }
    }
    ScreenCtx sc = { ox, oy, bw, bh, p->ox, p->oy, p->zoom };
    if (p->dragging) {
        int x0 = p->ax, y0 = p->ay, x1 = p->bx, y1 = p->by;
        switch (p->tool) {
        case T_LINE: ras_line(plot_screen, &sc, x0, y0, x1, y1, p->col, p->brush); break;
        case T_RECT: ras_rect(plot_screen, &sc, x0, y0, x1, y1, p->col, p->brush); break;
        case T_BOX: ras_box(plot_screen, &sc, x0, y0, x1, y1, p->col); break;
        case T_OVAL: ras_oval(plot_screen, &sc, x0, y0, x1, y1, p->col, p->brush); break;
        case T_DISC: ras_disc(plot_screen, &sc, x0, y0, x1, y1, p->col); break;
        }
    }
    if (p->tx >= 0) {
        for (int i = 0; p->text[i]; i++) {
            const u8 *gl = font_glyph(p->text[i]);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 8; x++)
                    if (gl[y] & (0x80 >> x)) plot_screen(&sc, p->tx + i * 8 + x, p->ty + y, p->col);
        }
        if (*api->gui_blink)
            ras_line(plot_screen, &sc, p->tx + strlen(p->text) * 8, p->ty + 2,
                     p->tx + strlen(p->text) * 8, p->ty + 13, p->col, 1);
    }

    int sy = cy + ch - SW_H;
    hline(cx, sy, cw, C_SHAD);
    panel(cx + 4, sy + 3, 28, 19, 1);
    fill_rect(cx + 7, sy + 6, 22, 13, p->col);
    int sw = (cw - 48) / 16;
    for (int i = 0; i < 16; i++) {
        int x = cx + 40 + i * sw;
        panel(x, sy + 3, sw - 1, 19, i == p->col);
        fill_rect(x + 3, sy + 6, sw - 7, 13, (u8)i);
    }
    hline(cx, cy + ch - 20, cw, C_SHAD);
    char status[96], zoom[12];
    int t = hover >= 0 ? hover : p->tool;
    if (p->msg[0] && hover < 0) strlcpy(status, p->msg, sizeof status);
    else kfmt(status, sizeof status, "%s: %s", tool_name[t], tool_help[t]);
    draw_text_clip(cx + 5, cy + ch - 17, status, C_BLACK, cw - 65);
    kfmt(zoom, sizeof zoom, "%d%%", p->zoom * 100);
    draw_text(cx + cw - 47, cy + ch - 17, zoom, C_DARK);

    if (p->mode != PM_NORM) {
        int x = cx + (p->mode == PM_SIZE ? 96 : (p->mode - PM_FILE) * 48);
        int y = cy + TOP_H;
        panel(x, y, 224, 84, 0);
        for (int i = 0; i < 4; i++) {
            int yy = y + 2 + i * 20;
            int hov = mx >= x + 2 && mx < x + 222 && my >= yy && my < yy + 20;
            int selected = p->mode == PM_VIEW && i < 3 && p->zoom == (1 << i);
            char size[32];
            const char *label;
            if (p->mode == PM_SIZE) {
                kfmt(size, sizeof size, "%d x %d pixels", psizes[i].w, psizes[i].h);
                label = size; selected = p->cw == psizes[i].w && p->ch == psizes[i].h;
            } else label = menus[p->mode - PM_FILE][i];
            menu_shade(api,x+2,yy,220,20,hov);
            if (selected) draw_char(x + 6, yy + 2, '*', hov ? C_WHITE : C_BLACK);
            draw_text(x + 22, yy + 2, label, hov ? C_WHITE : C_BLACK);
        }
    }
}

static void paint_csize(int inst, int *w, int *h)
{
    *w = paint_fit_w(&paints[inst]);
    *h = paint_fit_h(&paints[inst]);
}

static void paint_min(int *w, int *h)
{
    *w = MINW;
    *h = TOP_H + 208 + SW_H;
}

static int bmp_opener(const char *name, const char *fullpath,
                      const u8 *data, int n)
{
    int inst = win_open(paint_type);
    if (inst < 0) return -1;
    Paint *p = &paints[inst];
    if (paint_parse_bmp(p, data, n) != 0) return -1;
    p->has_file = 1;
    if (fullpath) { p->fsrc = 1; strlcpy(p->fpath, fullpath, sizeof p->fpath); }
    else          { p->fsrc = 0; strlcpy(p->fpath, name, sizeof p->fpath); }
    kfmt(p->msg, sizeof p->msg, "opened %s", pbase(p->fpath));
    p->mode = PM_NORM;
    win_fit_client(paint_type, inst, paint_fit_w(p), paint_fit_h(p));
    return 0;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, KEXT_RECLAIMABLE, "Paint"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;

    pdefw = psizes[2].w; pdefh = psizes[2].h;
    for (int i = 3; i >= 0; i--)
        if (psizes[i].w <= SW - 48 && psizes[i].h <= SH - 130) {
            pdefw = psizes[i].w; pdefh = psizes[i].h;
            break;
        }

    static const AppDesc d = {
        .title = "Paint", .max_inst = PAINT_INST, .resizable = 1, .in_menu = 1,
        .open = paint_reset, .close = paint_close, .draw = paint_draw, .key = paint_key,
        .mouse = paint_mouse, .client_size = paint_csize,
        .min_client = paint_min, .wheel = paint_wheel,
        .live_draw = APP_INDEPENDENT,
    };
    paint_type = api->register_app(&d);
    if (paint_type < 0) return 1;
    api->register_opener("bmp", bmp_opener);
    return 0;
}
