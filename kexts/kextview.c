#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1;
static int sel = -1, scroll;

#define HDR  26
#define ROWH 15
#define WINW 470
#define WINH 260

#define LIST_TOP  (HDR + 20)
#define LIST_BOT  24

static int list_rows(int ch)
{
    int r = (ch - LIST_TOP - LIST_BOT) / ROWH;
    return r < 0 ? 0 : r;
}

static void kv_clamp(int rows)
{
    int n = api->kext_count();
    if (scroll > n - rows) scroll = n - rows;
    if (scroll < 0) scroll = 0;
}

static void kv_scroll_to(int ypos, int rows, int ch)
{
    (void)ch;
    scroll = api->sbar_from_pos(rows * ROWH, api->kext_count(), rows, ypos);
    kv_clamp(rows);
    api->gui_dirty();
}

static const char *status_text(int st)
{
    switch (st) {
    case 0:  return "ok";
    case 40: return "E40 truncated";
    case 41: return "E41 unresolved symbol";
    case 42: return "E42 arena full";
    case 43: return "E43 newer kernel API required";
    case 44: return "E44 extension entry failed";
    case 45: return "E45 disabled after graphics fault";
    case 46: return "Unloaded (memory reserved)";
    default: return "failed";
    }
}

static void kv_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, HDR, GRGB(222, 226, 236),
                           GRGB(190, 196, 202), 1);
        gfx->set_dither(0);
    }
    api->hline(cx, cy + HDR - 1, cw, C_SHAD);

    int n = api->kext_count();
    u32 total = 0, bad = 0;
    for (int i = 0; i < n; i++) {
        const KextInfo *k = api->kext_get(i);
        if (!k) continue;
        total += k->size;
        if (k->status) bad++;
    }
    char b[80];
    api->kfmt(b, sizeof b, "%d extensions, %uK arena%s", n, (total + 1023) / 1024,
              bad ? ", SOME FAILED" : "");
    api->draw_text(cx + 8, cy + 6, b, bad ? C_MAROON : C_NAVY);

    int hy = cy + HDR + 2;
    api->draw_text(cx + 8,   hy, "File",   C_NAVY);
    api->draw_text(cx + 128, hy, "Name",   C_NAVY);
    api->draw_text(cx + 246, hy, "Kind",   C_NAVY);
    api->draw_text(cx + 296, hy, "Base",   C_NAVY);
    api->draw_text(cx + 360, hy, "Size",   C_NAVY);
    api->draw_text(cx + 410, hy, "Status", C_NAVY);
    api->hline(cx + 4, hy + 14, cw - 8, C_SHAD);

    int listy = cy + LIST_TOP;
    int rows = list_rows(ch);
    if (scroll > n - rows) scroll = n - rows;
    if (scroll < 0) scroll = 0;

    for (int r = 0; r < rows && scroll + r < n; r++) {
        int i = scroll + r;
        const KextInfo *k = api->kext_get(i);
        if (!k) continue;
        int ry = listy + r * ROWH;
        int on = (i == sel);
        if (on) api->fill_rect(cx + 4, ry, cw - 8 - SB_W, ROWH, C_HILITE);
        u8 fg = on ? C_WHITE : (k->status ? C_MAROON : C_BLACK);

        api->draw_text_clip(cx + 8, ry + 1, k->name, fg, 116);
        api->draw_text_clip(cx + 128, ry + 1, k->hname, fg, 114);
        api->draw_text(cx + 246, ry + 1,
                       k->kind == KEXT_KIND_KERNEL ? "kern" :
                       k->kind == KEXT_KIND_APP    ? "app"  : "-", fg);
        api->kfmt(b, sizeof b, "%x", k->base);
        api->draw_text(cx + 296, ry + 1, b, fg);
        api->kfmt(b, sizeof b, "%uK", (k->size + 1023) / 1024);
        api->draw_text(cx + 360, ry + 1, b, fg);
        api->draw_text(cx + 410, ry + 1, k->status ? "fail" : "ok",
                       on ? C_WHITE : (k->status ? C_MAROON : C_GREEN));
    }
    if (n > rows)
        api->draw_sbar(cx + cw - 4 - SB_W, listy, rows * ROWH, 0, n, rows, scroll);

    const KextInfo *k = sel >= 0 ? api->kext_get(sel) : 0;
    if (k) {
        api->kfmt(b, sizeof b, "%s: %s  %u bytes at %x..%x",
                  k->hname, status_text(k->status), k->size,
                  k->base, k->base + k->size);
        api->draw_text_clip(cx + 8, cy + ch - 16, b,
                            k->status ? C_MAROON : C_NAVY, cw - 16);
    } else {
        api->draw_text(cx + 8, cy + ch - 16,
                       "Select an extension for detail.", C_GRAY);
    }
}

static u8 kv_sbdrag;

static void kv_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst;
    int listy = LIST_TOP;
    int rows = list_rows(ch);
    if (ev == EV_RELEASE) { kv_sbdrag = 0; return; }
    if (ev == EV_DRAG) {
        if (kv_sbdrag) kv_scroll_to(ly - listy, rows, ch);
        return;
    }
    if (ev != EV_PRESS) return;
    if (ly < listy) return;
    if (lx >= cw - 4 - SB_W) {
        kv_sbdrag = 1;
        kv_scroll_to(ly - listy, rows, ch);
        return;
    }
    int r = (ly - listy) / ROWH;
    if (r < 0 || r >= rows || scroll + r >= api->kext_count()) return;
    sel = (sel == scroll + r) ? -1 : scroll + r;
    api->gui_dirty();
}

static void kv_wheel(int inst, int dz)
{
    (void)inst;
    scroll -= dz * 3;
    if (scroll < 0) scroll = 0;

    api->gui_dirty();
}

static void kv_key(int inst, int k)
{
    (void)inst;
    if (k == K_UP)   { if (scroll > 0) scroll--; api->gui_dirty(); }
    if (k == K_DOWN) { scroll++; api->gui_dirty(); }
}

static void kv_open(int inst) { (void)inst; sel = -1; scroll = 0; kv_sbdrag = 0; }
static void kv_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "KEXT Inspector"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "KEXT Inspector", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = kv_open, .draw = kv_draw, .mouse = kv_mouse, .key = kv_key,
        .wheel = kv_wheel,
        .client_size = kv_csize, .category = APP_CAT_DEV,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
