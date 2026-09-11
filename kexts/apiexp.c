/* Browses the API table generated from kapi.h. */
#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "apidoc.inc"
#include "apidoc_data.inc"
#include "textfield.inc"
#include "notebuf.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1;

#define HDR   26
#define ROWH  14
#define LINEH 18
#define COLW  8
#define WINW  600
#define WINH  386

static int listw_for(int cw)
{
    int w = cw * 190 / 520;
    if (w < 150) w = 150;
    if (w > 260) w = 260;
    return w;
}
#define LISTW listw_for(cw)
#define LIST_TOP (HDR + 26)

static int detail_scroll, detail_max, detail_sel = -1;
static int  sel;
static int  top;
static u8   follow = 1;

static char query[24];
static int  qlen, qcar;
static u8   qfocus = 1;

static short view[API_NENT];
static int   nview;

static void refilter(void)
{
    nview = 0;
    for (int i = 0; i < API_NENT; i++)
        if (ax_match(api_tab[i].name, query) ||
            ax_match(api_tab[i].sect, query) || ax_match(api_tab[i].doc, query))
            view[nview++] = (short)i;
    if (sel >= nview) sel = nview - 1;
    if (sel < 0) sel = 0;
}

static int rows_for(int ch) { int r = (ch - LIST_TOP - 6) / ROWH; return r < 0 ? 0 : r; }

static int wrap_text(int x, int y, int cols, const char *s, u8 col, int ylim,
                     int indent)
{
    (void)ylim;
    int len = api->strlen(s);
    int i = 0;
    while (i < len) {
        int e = nb_line_end(s, len, i, cols);
        char line[132];
        int m = e - i;
        if (m > (int)sizeof line - 1) m = (int)sizeof line - 1;
        for (int k = 0; k < m; k++) line[k] = s[i + k];
        line[m] = 0;
        api->draw_text(x + (i ? indent : 0), y, line, col);
        y += LINEH;
        int nx = nb_next_line(s, len, i, cols);
        if (nx <= i) break;
        i = nx;
    }
    return y;
}

static void ax_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_WHITE);
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, HDR, GRGB(214, 218, 228),
                           GRGB(180, 186, 202), 1);
        gfx->set_dither(0);
    }
    char t[40];
    api->kfmt(t, sizeof t, "Developer reference - %d entries", API_NENT);
    api->draw_text(cx + 10, cy + 8, t, C_NAVY);

    api->draw_text(cx + 10, cy + HDR + 7, "Find:", C_BLACK);
    int bx = cx + 54, by = cy + HDR + 4;
    api->panel(bx, by, LISTW - 50, 22, 1);
    api->fill_rect(bx + 1, by + 1, LISTW - 52, 20, C_WHITE);

    api->draw_text_clip(bx + 3, by + 3, query, C_BLACK, LISTW - 58);
    if (qfocus && *api->gui_blink) {
        int cxp = bx + 3 + qcar * 8;
        if (cxp < bx + LISTW - 55) api->fill_rect(cxp, by + 3, 1, 13, C_BLACK);
    }
    api->kfmt(t, sizeof t, "%d shown", nview);
    api->draw_text(cx + LISTW + 10, cy + HDR + 7, t, C_G0 + 4);

    int rows = rows_for(ch);

    top = follow ? ax_scroll(sel, top, rows, nview)
                 : ax_clamp_top(top, rows, nview);

    api->panel(cx + 6, cy + LIST_TOP - 2, LISTW, ch - LIST_TOP - 2, 1);
    api->set_clip(cx + 7, cy + LIST_TOP - 1, LISTW - 2, ch - LIST_TOP - 4);
    for (int r = 0; r < rows && top + r < nview; r++) {
        const ApiEnt *e = &api_tab[view[top + r]];

        int bar = cy + LIST_TOP + r * ROWH - 1;
        int on = (top + r) == sel;
        if (on) api->fill_rect(cx + 7, bar, LISTW - 2, ROWH, C_HILITE);
        api->draw_text_clip(cx + 11, bar + (ROWH - 16) / 2 + 1, e->name,
                            on ? C_WHITE : C_BLACK, LISTW - 12);
    }

    api->set_clip(cx, cy, cw, ch);
    if (nview > rows)
        api->draw_sbar(cx + 6 + LISTW - SB_W, cy + LIST_TOP - 2,
                       ch - LIST_TOP - 2, 0, nview, rows, top);

    int dx = cx + LISTW + 14, dw = cw - LISTW - 22;
    if (!nview) {
        api->draw_text(dx, cy + LIST_TOP + 4, "No matching entries.", C_G0 + 4);
        return;
    }
    const ApiEnt *e = &api_tab[view[sel]];
    if (detail_sel != view[sel]) { detail_scroll = 0; detail_sel = view[sel]; }
    api->set_clip(dx, cy + LIST_TOP, dw, ch - LIST_TOP - 26);
    int y = cy + LIST_TOP - detail_scroll;
    int cols = dw / COLW;
    if (cols < 8) cols = 8;

    api->draw_text_clip(dx, y, e->name, C_NAVY, dw); y += 22;
    if (e->doc[0]) { y = wrap_text(dx, y, cols, e->doc, C_BLACK, cy + ch - 20, 0); y += 10; }
    api->draw_text(dx, y, "C declaration", C_G0 + 2);
    y += LINEH + 2;

    char sig[224];
    if (e->is_fn)
        api->kfmt(sig, sizeof sig, "%s %s(%s)", e->ret, e->name, e->args);
    else
        api->kfmt(sig, sizeof sig, "%s %s", e->ret, e->name);
    y = wrap_text(dx, y, cols, sig, C_NAVY, cy + ch - 16, 0);

    api->draw_text_clip(dx, y + 2, e->sect, C_G0 + 4, dw);
    y += 18;
    api->hline(dx, y, dw, C_G0 + 6);
    y += 8;

    if (!e->is_fn) {
        api->draw_text_clip(dx, y, "Read this value directly.",
                                          C_G0 + 3, dw);
        y += LINEH + 4;
    } else if (!e->nargs) {
        api->draw_text_clip(dx, y, "No input values are needed.",
                                          C_G0 + 3, dw);
        y += LINEH + 4;
    } else {
        api->draw_text(dx, y, "Input values", C_BLACK);
        y += LINEH + 2;

        const char *p = e->args;
        int depth = 0, o = 0;
        char one[96];
        for (;;) {
            char c = *p;
            if (c == '(') depth++;
            else if (c == ')') depth--;
            if (!c || (c == ',' && depth == 0)) {
                one[o] = 0;
                if (o)
                    y = wrap_text(dx + 12, y, cols - 2, one, C_BLACK,
                                  cy + ch - LINEH, 12);
                o = 0;
                if (!c) break;
                p++;
                while (*p == ' ') p++;
                continue;
            }
            if (o < (int)sizeof one - 1) one[o++] = c;
            p++;
        }
        y += 4;
    }

    detail_max = y + detail_scroll - (cy + ch - 28);
    if (detail_max < 0) detail_max = 0;
    if (detail_scroll > detail_max) { detail_scroll = detail_max; api->gui_dirty(); }
    api->set_clip(cx, cy, cw, ch);
    if (detail_max) {
        api->draw_text_clip(dx, cy + ch - 20, "Scroll details:", C_G0 + 3, dw - 102);
        ui_button(cx, cy, ui_r(cw - 102, ch - 24, 44, 22), "Up", 0, detail_scroll > 0);
        ui_button(cx, cy, ui_r(cw - 54, ch - 24, 48, 22), "Down", 0, detail_scroll < detail_max);
    }
}

static void ax_key(int inst, int k)
{
    (void)inst;
    follow = 1;
    if (k == K_UP)   { if (sel > 0) sel--; api->gui_dirty(); return; }
    if (k == K_DOWN) { if (sel < nview - 1) sel++; api->gui_dirty(); return; }
    if (k == K_PGUP) { sel -= 10; if (sel < 0) sel = 0; api->gui_dirty(); return; }
    if (k == K_PGDN) { sel += 10; if (sel > nview - 1) sel = nview - 1;
                       api->gui_dirty(); return; }
    if (k == K_HOME) { sel = 0; api->gui_dirty(); return; }
    if (k == K_END)  { sel = nview - 1; api->gui_dirty(); return; }

    TextField f;
    f.buf = query; f.cap = sizeof query; f.len = qlen; f.caret = qcar; f.all = 0;
    int printable = k >= 32 && k < 127;
    int oldlen = qlen;
    if (tf_key(&f, k, printable)) {
        int changed = (f.len != oldlen);
        qlen = f.len;
        qcar = f.caret;

        if (changed) { sel = 0; top = 0; detail_sel = -1; refilter(); }
        api->gui_dirty();
    }
}

static void ax_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw;
    if (ev != EV_PRESS && ev != EV_DRAG) return;
    if (ev == EV_PRESS && ly >= ch - 24 && lx >= cw - 102 && detail_max) {
        detail_scroll += lx < cw - 56 ? -72 : 72;
        if (detail_scroll < 0) detail_scroll = 0;
        if (detail_scroll > detail_max) detail_scroll = detail_max;
        api->gui_dirty(); return;
    }
    int rows = rows_for(ch);
    int list_h = ch - LIST_TOP - 2;
    int sb_x = 6 + LISTW - SB_W;

    if (nview > rows && lx >= sb_x && lx < 6 + LISTW && ly >= LIST_TOP - 2) {
        top = api->sbar_from_pos(list_h, nview, rows, ly - (LIST_TOP - 2));
        top = ax_clamp_top(top, rows, nview);
        follow = 0;
        api->gui_dirty();
        return;
    }
    if (ev != EV_PRESS) return;
    qfocus = 1;

    if (lx >= 6 && lx < sb_x && ly >= LIST_TOP - 1) {
        int r = (ly - (LIST_TOP - 1)) / ROWH;
        if (r >= 0 && r < rows && top + r < nview) {
            sel = top + r;
            follow = 1;
            api->gui_dirty();
        }
    }
}

static void ax_wheel(int inst, int dz)
{
    (void)inst;
    follow = 1;
    sel -= dz * 3;
    if (sel < 0) sel = 0;
    if (sel > nview - 1) sel = nview - 1;
    api->gui_dirty();
}

static void ax_open(int inst)
{
    (void)inst;
    sel = top = detail_scroll = detail_max = 0; detail_sel = -1;
    query[0] = 0;
    qlen = qcar = 0;
    qfocus = 1;
    refilter();
}

static void ax_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }
static void ax_min(int *w, int *h) { *w = 380; *h = 200; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "API Explorer"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11); ui_init(k, gfx);
    static const AppDesc d = {
        .title = "API Explorer", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = ax_open, .draw = ax_draw, .mouse = ax_mouse, .key = ax_key,
        .wheel = ax_wheel, .client_size = ax_csize, .min_client = ax_min,
        .category = APP_CAT_DEV,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
