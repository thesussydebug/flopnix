#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "sbdrag.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1;
static int scroll;
static SbDrag sbd;
static int list_y0, list_len, list_n;

#define WINW  560
#define WINH  420
#define BAR_Y (UI_HDR + 30)
#define BAR_H 26
#define GA_Y  (BAR_Y + BAR_H + 30)
#define GA_H  140
#define GB_Y  (GA_Y + GA_H + 14)
#define ROWH  14

typedef struct { GRGB a, b; } Shade;
static const Shade SH_[] = {
    { GRGB( 60, 84,168), GRGB( 26, 40,104) },
    { GRGB(168,156, 56), GRGB(110, 98, 24) },
    { GRGB(176, 72, 72), GRGB(112, 32, 32) },
    { GRGB(150, 92,180), GRGB( 92, 44,124) },
    { GRGB( 64,164,164), GRGB( 24,104,108) },
    { GRGB( 76,140,220), GRGB( 30, 78,160) },
    { GRGB(120,150,100), GRGB(65,90,45) },
};

static void grad(int x, int y, int w, int h, GRGB a, GRGB b, int vert, u8 fb)
{
    if (w <= 0 || h <= 0) return;
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(x, y, w, h, a, b, vert);
        gfx->set_dither(0);
    } else api->fill_rect(x, y, w, h, fb);
}

typedef struct { int lo, hi; u8 col; const char *name; } Region;
static const Region RG[] = {
    { MI_KERNEL_BASE, MI_KERNEL_END, C_NAVY,   "Operating system" },
    { MI_DMA_BASE,    MI_DMA_END,    C_OLIVE,  "Floppy transfers" },
    { MI_FB_BASE,     MI_FB_END,     C_MAROON, "Screen drawing" },
    { MI_ARENA_BASE,  MI_ARENA_END,  C_PURPLE, "Apps and drivers" },
    { MI_POOL_BASE,   MI_POOL_END,   C_TEAL,   "App workspaces" },
    { MI_HEAP_BASE,   MI_HEAP_END,   C_BBLUE,  "Temporary buffers" },
    { MI_IO_BASE,     MI_IO_END,     C_GREEN,  "File transfers" },
};
#define NRG ((int)(sizeof RG / sizeof RG[0]))

static u32 region_capacity(int i) {
    if (!i) return api->mem_info(MI_KERNEL_BYTES);
    return api->mem_info(RG[i].hi) - api->mem_info(RG[i].lo);
}
static int region_used(int i, u32 *used, u32 *cap)
{
    *cap = region_capacity(i);
    switch (RG[i].lo) {
    case MI_ARENA_BASE:
        *used = api->mem_info(MI_ARENA_RO) + api->mem_info(MI_ARENA_RW);
        return 1;
    case MI_POOL_BASE:
        *used = api->mem_info(MI_POOL_USED);
        return 1;
    case MI_HEAP_BASE:
        *used = *cap - api->mem_info(MI_HEAP_FREE);
        return 1;
    default:
        return 0;
    }
}

static void mm_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

static int list_rows(int ch)
{
    int h = ch - GB_Y - 20 - UI_GTOP - 4;
    return h > 0 ? h / ROWH : 0;
}

static void mm_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    ui_header(cx, cy, cw, "Memory Map");

    char b[80], hs[16];
    u32 memkb = api->mem_info(MI_TOTAL_KB);
    if (!memkb) memkb = api->mem_total_kb();
    api->human_size_kb(memkb, hs, sizeof hs);
    api->kfmt(b, sizeof b, "%s usable", hs);
    ui_header_right(cx, cy, cw, b, C_G0 + 3);

    int bw = cw - 24, bx = cx + 12, by = cy + BAR_Y;
    api->panel(bx - 1, by - 1, bw + 2, BAR_H + 2, 1);
    grad(bx, by, bw, BAR_H, GRGB(228, 232, 238), GRGB(198, 204, 214), 1,
         C_G0 + 6);
    u32 offset = 0;
    for (int i = 0; i < NRG; i++) {
        u32 lo = offset, hi = offset + region_capacity(i);
        offset = hi;
        int x0 = (int)((lo >> 10) * (u32)bw / memkb);
        int x1 = (int)((hi >> 10) * (u32)bw / memkb);
        if (x1 > bw) x1 = bw;
        if (x1 <= x0) x1 = x0 + 1;
        grad(bx + x0, by, x1 - x0, BAR_H, SH_[i].a, SH_[i].b, 1, RG[i].col);

        u32 used, cap;
        if (region_used(i, &used, &cap) && cap) {
            int uw = (int)((u32)(x1 - x0) * used / cap);
            if (uw > 0) {
                grad(bx + x0, by, uw, BAR_H, SH_[i].b, SH_[i].b, 1, RG[i].col);
                api->vline(bx + x0 + uw, by, BAR_H, C_WHITE);
            }
        }
        if (x1 - x0 > 1) api->vline(bx + x1 - 1, by, BAR_H, C_SHAD);
    }
    api->draw_text(cx + 12, by + BAR_H + 5, "Reserved", C_G0 + 3);
    api->human_size_kb(memkb, b, sizeof b);
    api->draw_text(cx + cw - 12 - api->text_width(b), by + BAR_H + 5, b, C_G0 + 3);

    u32 claimed = 0;
    for (int i = 0; i < NRG; i++)
        claimed += region_capacity(i);
    api->kfmt(b, sizeof b, "%u%% set aside for the system", (unsigned)((claimed >> 10) * 100 / memkb));
    api->draw_text(cx + (cw - api->text_width(b)) / 2, by + BAR_H + 5, b, C_G0 + 2);

    ui_group(cx + 12, cy + GA_Y, cw - 24, GA_H, "Where memory is reserved");
    int x = ui_gx(cx + 12), y = ui_gy(cy + GA_Y);
    for (int i = 0; i < NRG; i++, y += ROWH) {
        u32 used, cap;
        int has = region_used(i, &used, &cap);
        grad(x, y + 1, 9, 9, SH_[i].a, SH_[i].b, 1, RG[i].col);
        api->bevel(x - 1, y, 11, 11, 1);
        api->draw_text_clip(x + 15, y, RG[i].name, C_BLACK, cw / 2 - 28);
        char sz[16];
        api->human_size(cap, sz, sizeof sz);
        api->draw_text(x + cw / 2 - 6, y, sz, C_G0 + 2);
        int mw = 68, mx0 = cx + cw - 124;
        if (has && cap) {
            api->panel(mx0, y + 1, mw, 9, 1);
            int fw = (int)((u32)(mw - 2) * used / cap);
            if (fw > 0) grad(mx0 + 1, y + 2, fw, 7, SH_[i].a, SH_[i].b, 0, RG[i].col);
            api->kfmt(b, sizeof b, "%u%%", (unsigned)((u32)used * 100 / cap));
            api->draw_text(mx0 + mw + 6, y, b, C_G0 + 2);
        } else {
            api->draw_text(mx0 + mw / 2 - 4, y, "-", C_G0 + 4);
        }
    }

    u32 aro = api->mem_info(MI_ARENA_RO), arw = api->mem_info(MI_ARENA_RW);
    u32 acap = api->mem_info(MI_ARENA_END) - api->mem_info(MI_ARENA_BASE);
    api->kfmt(b, sizeof b, "Space for more app code: %u KB", (acap - aro - arw) / 1024);
    api->draw_text_clip(x, y, b, C_PURPLE, cw - 44);
    y += ROWH;

    char hf[16], hl[16];
    api->human_size(api->mem_info(MI_HEAP_FREE), hf, sizeof hf);
    api->human_size(api->mem_info(MI_HEAP_LARGEST), hl, sizeof hl);
    api->kfmt(b, sizeof b, "Free working memory: %s", hf);
    api->draw_text_clip(x, y, b, C_BBLUE, cw - 44);

    int n = api->kext_count();
    u32 kused = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        const KextInfo *k = api->kext_get(i);
        if (!k) break;
        if (k->status) failed++;
        else kused += k->size;
    }
    api->kfmt(b, sizeof b, "Loaded components: %d (%u KB)", n - failed,
              (kused + 1023) / 1024);
    int gh = ch - GB_Y - 20 - 4;
    ui_group(cx + 12, cy + GB_Y, cw - 24, gh, b);
    int rows = list_rows(ch);
    if (scroll > n - rows) scroll = n - rows;
    if (scroll < 0) scroll = 0;
    x = ui_gx(cx + 12);
    y = ui_gy(cy + GB_Y);
    for (int i = scroll; i < n && i < scroll + rows; i++, y += ROWH) {
        const KextInfo *k = api->kext_get(i);
        if (!k) break;
        if ((i - scroll) & 1)

            api->fill_rect(x - 4, y - 1, cw - 36, ROWH, C_G0 + 7);
        if (k->status) {
            api->kfmt(b, sizeof b, "%-28s  load failed (E%d)", k->name, k->status);
            api->draw_text_clip(x, y, b, C_RED, cw - 48);
        } else {
            api->draw_text_clip(x, y, k->hname, C_BLACK, 176);
            char sz[16];
            api->human_size(k->size, sz, sizeof sz);
            api->draw_text(x + 186, y, sz, C_NAVY);
            api->draw_text_clip(x + 250, y, k->name, C_G0 + 3, cw - 300);
        }
    }
    if (!n) api->draw_text(x, y, "(no extensions loaded)", C_G0 + 3);

    list_y0  = cy + GB_Y + UI_GTOP;
    list_len = rows * ROWH;
    list_n   = n;
    if (n > rows)
        api->draw_sbar(cx + cw - 12 - SB_W, list_y0, list_len, 0, n, rows, scroll);

    ui_status(cx, cy, cw, ch,
              "Bars show how much of each reserved area is in use.");
}

static void mm_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst;
    int rows = list_rows(ch);
    if (rows < 1) return;

    int sbx = cw - 12 - SB_W;
    int pos = ly - (GB_Y + UI_GTOP);

    if (ev == EV_DRAG) {
        if (sbd.active) {
            scroll = sb_move(&sbd, list_len, list_n, rows, pos);
            api->gui_dirty();
        }
        return;
    }
    if (ev == EV_RELEASE) { sbd.active = 0; return; }
    if (ev != EV_PRESS) return;
    sbd.active = 0;
    if (ly < GB_Y) return;

    if (lx >= sbx && list_n > rows) {
        scroll = sb_press(&sbd, list_len, list_n, rows, scroll, pos);
        api->gui_dirty();
        return;
    }
    scroll += (ly > GB_Y + rows * ROWH / 2) ? rows / 2 : -(rows / 2);
    api->gui_dirty();
}

static void mm_wheel(int inst, int dz)
{
    (void)inst;
    scroll -= dz * 3;
    api->gui_dirty();
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Memory Map"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    ui_init(k, gfx);
    static const AppDesc d = {
        .title = "Memory Map", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .draw = mm_draw, .client_size = mm_csize,
        .mouse = mm_mouse, .wheel = mm_wheel,
        .category = APP_CAT_SYSTEM,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
