#include "kapi.h"
#include "gdi.h"
#include "button.h"
#include "mepreset.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;

#define MAPPED(a) (api->mem_mapped((u32)(a)))

#define WRITABLE(a) (api->mem_writable((u32)(a)))
#define RD(a)     (*(volatile u8 *)(u32)(a))

#define WR(a, v)  (api->mem_poke((u32)(a), (u8)(v)))

#define TB_H   30
#define HDR_H  20
#define ROW_H  16
#define FOOT_H 22
#define HX_OFF (6 + 9 * 8)

static MePreset presets[8];
static int npreset;

static void build_presets(void)
{
    npreset = me_build_presets(api->mem_info(MI_KERNEL_BASE),
                               api->mem_info(MI_DMA_BASE),
                               api->mem_info(MI_FB_BASE),
                               api->mem_info(MI_ARENA_BASE),
                               api->mem_info(MI_HEAP_BASE),
                               api->mem_info(MI_POOL_BASE),
                               api->mem_info(MI_HEAP_GROW_BASE),
                               presets, 8);
}
#define NPRESET npreset

static int preset_fit(int cw)
{
    int fits = (cw - 134) / 50;
    if (fits < 0) fits = 0;
    return fits < npreset ? fits : npreset;
}

static u32 base = 0x400000;
static u32 cursor = 0x400000;
static int nibble;
static int ascii_mode;
static int editing_goto;
static char goto_buf[9];
static int goto_len;
static int cur_bpr = 16, cur_cw, cur_ch;

static int hexv(int k)
{
    if (k >= '0' && k <= '9') return k - '0';
    k |= 32;
    if (k >= 'a' && k <= 'f') return k - 'a' + 10;
    return -1;
}

static u32 phex(const char **pp)
{
    const char *p = *pp;
    u32 v = 0;
    if (p[0] == '0' && (p[1] | 32) == 'x') p += 2;
    for (int d; (d = hexv(*p)) >= 0; p++) v = v * 16 + d;
    *pp = p;
    return v;
}

static int bpr_for(int cw) { return cw >= 612 ? 16 : 8; }
static int vis_rows(int ch)
{
    int r = (ch - TB_H - HDR_H - FOOT_H) / ROW_H;
    return r < 1 ? 1 : r;
}
static int byte_xoff(int c, int bpr) { return HX_OFF + c * 24 + (bpr > 8 && c >= 8 ? 8 : 0); }
static int asc_xoff(int c, int bpr)  { return HX_OFF + bpr * 24 + (bpr > 8 ? 8 : 0) + 8 + c * 8; }

static void follow(int rows, int bpr)
{
    if (cursor < base) base = cursor - cursor % bpr;
    else if (cursor >= base + (u32)rows * bpr)
        base = (cursor - cursor % bpr) - (u32)(rows - 1) * bpr;
}

static void tick_refresh(void *ctx)
{
    (void)ctx;
    api->win_redraw(my_type, 0);
}

static void me_draw(Win *w, int cx, int cy, int cw, int ch)
{
    int bpr = bpr_for(cw);
    int rows = vis_rows(ch);
    cur_bpr = bpr; cur_cw = cw; cur_ch = ch;
    int foc = api->win_is_focused(w);
    char b[56];

    api->fill_rect(cx, cy, cw, ch, C_WHITE);

    api->fill_rect(cx, cy, cw, TB_H, C_FACE);
    api->hline(cx, cy + TB_H - 1, cw, C_SHAD);
    api->draw_text(cx + 6, cy + 6, "Goto", C_BLACK);
    api->panel(cx + 42, cy + 4, 84, 22, 1);
    api->fill_rect(cx + 44, cy + 6, 80, 18, C_WHITE);
    if (editing_goto) {
        api->kfmt(b, sizeof b, "%s%s", goto_buf, *api->gui_blink ? "_" : "");
        api->draw_text(cx + 46, cy + 6, b, C_BLACK);
    } else {
        api->kfmt(b, sizeof b, "%08x", base);
        api->draw_text(cx + 46, cy + 6, b, C_G0 + 2);
    }
    for (int c = 0; c < preset_fit(cw); c++) {
        int bx = cx + 134 + c * 50;
        button_label(api,bx,cy+4,48,22,presets[c].label,base==presets[c].addr,1);
    }

    int ry = cy + TB_H;
    api->fill_rect(cx, ry, cw, HDR_H, C_FACE);
    api->draw_text(cx + 6, ry + 2, "Address", C_NAVY);
    api->hline(cx, ry + HDR_H - 1, cw, C_SHAD);
    for (int c = 0; c < bpr; c++) {
        api->kfmt(b, sizeof b, "%02x", c);
        api->draw_text(cx + byte_xoff(c, bpr), ry + 2, b, C_NAVY);
    }
    api->draw_text(cx + asc_xoff(0, bpr), ry + 2, "ASCII", C_NAVY);

    int gy = cy + TB_H + HDR_H;
    api->fill_rect(cx, gy, HX_OFF - 7, rows * ROW_H, C_G0 + 7);
    for (int v = 0; v < rows; v++) {
        u32 addr = base + (u32)v * bpr;
        int y = gy + v * ROW_H;
        if (v & 1) api->fill_rect(cx + HX_OFF - 6, y, cw - HX_OFF + 6, ROW_H, C_G0 + 7);
        api->kfmt(b, sizeof b, "%08x", addr);
        api->draw_text(cx + 6, y, b, C_NAVY);
        for (int c = 0; c < bpr; c++) {
            u32 a = addr + c;
            int ok = MAPPED(a);
            u8 val = ok ? RD(a) : 0;
            int bx = cx + byte_xoff(c, bpr);
            int ax = cx + asc_xoff(c, bpr);
            int sel = (a == cursor);
            char hb[4];
            if (ok) api->kfmt(hb, sizeof hb, "%02x", val);
            else    api->strlcpy(hb, "--", sizeof hb);
            u8 hfg;
            if (sel && !ascii_mode) { api->fill_rect(bx - 1, y, 17, ROW_H, C_NAVY); hfg = C_WHITE; }
            else if (sel)           { api->rect(bx - 1, y, 17, ROW_H, C_NAVY); hfg = C_BLACK; }
            else                    hfg = !ok ? C_G0 + 2 : val ? C_BLACK : C_G0 + 4;
            api->draw_text(bx, y, hb, hfg);
            if (sel && !ascii_mode && foc && *api->gui_blink)
                api->fill_rect(bx + nibble * 8, y + 14, 7, 2, C_YELLOW);

            char ac = !ok ? ' ' : (val >= 32 && val < 127) ? (char)val : '.';
            u8 afg;
            if (sel && ascii_mode) { api->fill_rect(ax - 1, y, 9, ROW_H, C_NAVY); afg = C_WHITE; }
            else if (sel)          { api->rect(ax - 1, y, 9, ROW_H, C_NAVY); afg = C_BLACK; }
            else                   afg = (val >= 32 && val < 127) ? C_BLACK : C_G0 + 3;
            api->draw_char(ax, y, ac, afg);
        }
    }

    api->vline(cx + HX_OFF - 7, gy, rows * ROW_H, C_SHAD);
    api->vline(cx + asc_xoff(0, bpr) - 5, gy, rows * ROW_H, C_G0 + 5);

    int fy = cy + ch - FOOT_H;
    api->fill_rect(cx, fy, cw, FOOT_H, C_FACE);
    api->hline(cx, fy, cw, C_LIGHT);
    if (!MAPPED(cursor)) {
        api->kfmt(b, sizeof b, "@%08x   not mapped   [%s]",
                  cursor, ascii_mode ? "ASCII" : "hex");
    } else {
        u8 cv = RD(cursor);
        char cc = (cv >= 32 && cv < 127) ? (char)cv : '.';
        api->kfmt(b, sizeof b, "@%08x = 0x%02x  %u  '%c'   [%s]%s",
                  cursor, cv, cv, cc, ascii_mode ? "ASCII" : "hex",

                  WRITABLE(cursor) ? "" : "  raw write");
    }
    api->draw_text_clip(cx + 6, fy + 3, b, C_BLACK, cw - 12);
}

static void goto_commit(void)
{
    const char *p = goto_buf;
    u32 a = phex(&p);
    editing_goto = 0;
    cursor = a;
    base = a - a % cur_bpr;
    nibble = 0;
}

static void me_key(int inst, int k)
{
    (void)inst;
    int bpr = cur_bpr, rows = vis_rows(cur_ch);

    if (editing_goto) {
        if (k == '\n') goto_commit();
        else if (k == 27) editing_goto = 0;
        else if (k == '\b') { if (goto_len) goto_buf[--goto_len] = 0; }
        else if (hexv(k) >= 0 && goto_len < 8) {
            goto_buf[goto_len++] = (char)k;
            goto_buf[goto_len] = 0;
        }
        return;
    }

    switch (k) {
    case K_LEFT:  if (cursor) cursor--; nibble = 0; break;
    case K_RIGHT: cursor++; nibble = 0; break;
    case K_UP:    if (cursor >= (u32)bpr) cursor -= bpr; nibble = 0; break;
    case K_DOWN:  cursor += bpr; nibble = 0; break;
    case K_HOME:  cursor -= cursor % bpr; nibble = 0; break;
    case K_END:   cursor = cursor - cursor % bpr + bpr - 1; nibble = 0; break;
    case K_PGUP: {
        u32 d = (u32)rows * bpr;
        base = base >= d ? base - d : 0;
        if (cursor >= d) cursor -= d; else cursor %= bpr;
        nibble = 0; break;
    }
    case K_PGDN: {
        u32 d = (u32)rows * bpr;
        base += d; cursor += d; nibble = 0; break;
    }
    case '\t': ascii_mode = !ascii_mode; nibble = 0; break;
    default:
        if (!ascii_mode && (k == 'g' || k == 'G')) {
            editing_goto = 1; goto_len = 0; goto_buf[0] = 0;
        } else if (ascii_mode) {

            if (k >= 32 && k < 127 && MAPPED(cursor)) {
                WR(cursor, k); cursor++;
            }
        } else {
            int val = hexv(k);
            if (val >= 0 && MAPPED(cursor)) {
                if (!nibble) { WR(cursor, (RD(cursor) & 0x0F) | (val << 4)); nibble = 1; }
                else         { WR(cursor, (RD(cursor) & 0xF0) | val); nibble = 0; cursor++; }
            }
        }
    }
    follow(rows, bpr);
}

static void me_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst;
    int bpr = bpr_for(cw);
    cur_bpr = bpr; cur_cw = cw; cur_ch = ch;
    if (ev != EV_PRESS) return;

    if (ly >= 4 && ly < 26) {

        if (lx >= 42 && lx < 126) {
            editing_goto = 1; goto_len = 0; goto_buf[0] = 0;
            return;
        }
        for (int c = 0; c < preset_fit(cw); c++) {
            int bx = 134 + c * 50;
            if (lx >= bx && lx < bx + 48) {
                base = presets[c].addr; cursor = base;
                nibble = 0; editing_goto = 0;
                return;
            }
        }
        return;
    }

    int gy = TB_H + HDR_H;
    if (ly < gy) return;
    int rows = vis_rows(ch);
    int row = (ly - gy) / ROW_H;
    if (row < 0 || row >= rows) return;
    for (int c = 0; c < bpr; c++) {
        int x = byte_xoff(c, bpr);
        if (lx >= x - 1 && lx < x + 17) {
            cursor = base + (u32)row * bpr + c;
            ascii_mode = 0; nibble = 0; editing_goto = 0;
            return;
        }
    }
    for (int c = 0; c < bpr; c++) {
        int x = asc_xoff(c, bpr);
        if (lx >= x - 1 && lx < x + 8) {
            cursor = base + (u32)row * bpr + c;
            ascii_mode = 1; nibble = 0; editing_goto = 0;
            return;
        }
    }
}

static void me_wheel(int inst, int dz)
{
    (void)inst;
    u32 step = (u32)cur_bpr * 3;
    if (dz > 0) base = base >= step ? base - step : 0;
    else        base += step;
}

static void me_open(int inst)
{
    (void)inst;
    build_presets();

    base = cursor = api->mem_info(MI_ARENA_BASE);
    if (!base) base = cursor = 0x400000;
    nibble = ascii_mode = editing_goto = 0;
    if (timer_id < 0) timer_id = api->timer_add(15, tick_refresh, 0);
}

static void me_close(int inst)
{
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

static void me_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = (*api->screen_w >= 620) ? 612 : 348;
    *h = TB_H + HDR_H + 16 * ROW_H + FOOT_H;
}
static void me_min(int *w, int *h)
{
    *w = 348;
    *h = TB_H + HDR_H + 4 * ROW_H + FOOT_H;
}

static void cmd_peek(const char *args)
{
    while (*args == ' ') args++;
    const char *p = args;
    u32 a = phex(&p);
    while (*p == ' ') p++;
    int n = *p ? api->atoi(p) : 64;
    if (n < 1) n = 1;
    if (n > 512) n = 512;
    char line[96], cell[8];
    for (int off = 0; off < n; off += 16) {
        api->kfmt(line, sizeof line, "%08x  ", a + off);
        int o = (int)api->strlen(line);
        for (int c = 0; c < 16; c++) {
            u32 ad = a + off + c;
            if (off + c >= n)     api->strlcpy(cell, "   ", sizeof cell);
            else if (!MAPPED(ad)) api->strlcpy(cell, "-- ", sizeof cell);
            else                  api->kfmt(cell, sizeof cell, "%02x ", RD(ad));
            for (int j = 0; cell[j]; j++) line[o++] = cell[j];
        }
        line[o++] = ' ';
        for (int c = 0; c < 16 && off + c < n; c++) {
            u32 ad = a + off + c;
            if (!MAPPED(ad)) { line[o++] = ' '; continue; }
            u8 v = RD(ad);
            line[o++] = (v >= 32 && v < 127) ? (char)v : '.';
        }
        line[o++] = '\n';
        line[o] = 0;
        api->shell_print(line);
    }
}

static void cmd_poke(const char *args)
{
    while (*args == ' ') args++;
    const char *p = args;
    u32 a = phex(&p);
    int wrote = 0, blocked = 0;
    for (;;) {
        while (*p == ' ') p++;
        if (hexv(*p) < 0) break;
        u32 v = phex(&p);

        if (!WR(a + wrote, v)) { blocked = 1; break; }
        wrote++;
    }
    char b[64];
    if (blocked) api->kfmt(b, sizeof b, "%08x is not mapped - wrote %d\n",
                           a + wrote, wrote);
    else if (wrote) api->kfmt(b, sizeof b, "wrote %d byte%s at %08x\n",
                         wrote, wrote == 1 ? "" : "s", a);
    else       api->strlcpy(b, "usage: poke <addr> <hex byte>...\n", sizeof b);
    api->shell_print(b);
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Memory Editor"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Memory Editor", .max_inst = 1, .resizable = 1, .in_menu = 1,
        .open = me_open, .close = me_close, .draw = me_draw, .key = me_key, .mouse = me_mouse,
        .wheel = me_wheel, .client_size = me_csize, .min_client = me_min,
    };
    my_type = k->register_app(&d);
    k->register_cmd("peek", "peek <addr> [count] - hex-dump memory (hex addr)",
                    cmd_peek);
    k->register_cmd("poke", "poke <addr> <hex byte>... - write memory bytes",
                    cmd_poke);
    return my_type < 0;
}
