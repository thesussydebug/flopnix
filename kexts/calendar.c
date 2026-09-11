#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;

static int view_y, view_m;

static int dow(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int days_in(int y, int m)
{
    static const u8 dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

static const char *const MON[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

#define CELLW 30
#define CELLH 18
#define GRIDX 8
#define GRIDY 46

static void cal_open(int inst)
{
    (void)inst;
    int h, m, s, D, M, Y;
    api->rtc_read(&h, &m, &s, &D, &M, &Y);

    if (M < 1 || M > 12) M = 1;
    if (Y < 1900 || Y > 2999) Y = 2000;
    view_y = Y;
    view_m = M;
}

static void cal_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w; (void)ch;
    if (gfx) { gfx->set_dither(1); gfx->fill_gradient(cx, cy, cw, 34, GRGB(214, 218, 228), GRGB(180, 186, 202), 1); gfx->set_dither(0); }
    int h, m, s, tD, tM, tY;
    api->rtc_read(&h, &m, &s, &tD, &tM, &tY);

    int hovl = *api->mouse_x >= cx + 8 && *api->mouse_x < cx + 26 &&
               *api->mouse_y >= cy + 8 && *api->mouse_y < cy + 26;
    int hovr = *api->mouse_x >= cx + cw - 26 && *api->mouse_x < cx + cw - 8 &&
               *api->mouse_y >= cy + 8 && *api->mouse_y < cy + 26;
    api->panel(cx + 8, cy + 8, 18, 18, hovl);
    api->draw_char(cx + 13, cy + 9, '<', C_NAVY);
    api->panel(cx + cw - 26, cy + 8, 18, 18, hovr);
    api->draw_char(cx + cw - 21, cy + 9, '>', C_NAVY);
    char hd[24];
    api->kfmt(hd, sizeof hd, "%s %d", MON[view_m - 1], view_y);
    api->draw_text(cx + (cw - (int)api->strlen(hd) * 8) / 2, cy + 10, hd, C_BLACK);

    static const char *const WD = "SMTWTFS";
    for (int c = 0; c < 7; c++)
        api->draw_char(cx + GRIDX + c * CELLW + 11, cy + 30,
                       WD[c], c == 0 || c == 6 ? C_MAROON : C_NAVY);

    int first = dow(view_y, view_m, 1);
    int nd = days_in(view_y, view_m);
    int today = (view_y == tY && view_m == tM) ? tD : 0;
    for (int d = 1; d <= nd; d++) {
        int cell = first + d - 1;
        int px = cx + GRIDX + (cell % 7) * CELLW;
        int py = cy + GRIDY + (cell / 7) * CELLH;
        char b[4];
        api->kfmt(b, sizeof b, "%d", d);
        int tx = px + (CELLW - (int)api->strlen(b) * 8) / 2;
        if (d == today) {
            api->fill_rect(px + 2, py - 1, CELLW - 4, CELLH - 2, C_NAVY);
            api->draw_text(tx, py, b, C_WHITE);
        } else
            api->draw_text(tx, py, b, cell % 7 == 0 || cell % 7 == 6
                                      ? C_MAROON : C_BLACK);
    }
}

static void cal_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)ch;
    if (ev != EV_PRESS || ly < 8 || ly >= 26) return;

    int m = view_m, y = view_y;
    if (lx >= 8 && lx < 26) {
        if (y > 1900 || m > 1) { if (--m < 1) { m = 12; y--; } }
    } else if (lx >= cw - 26 && lx < cw - 8) {
        if (y < 2999 || m < 12) { if (++m > 12) { m = 1; y++; } }
    } else return;
    view_m = m;
    view_y = y;
}

static void cal_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = GRIDX * 2 + 7 * CELLW;
    *h = GRIDY + 6 * CELLH + 8;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Calendar"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Calendar", .max_inst = 1, .in_menu = 1,
        .open = cal_open, .draw = cal_draw, .mouse = cal_mouse,
        .client_size = cal_csize,
    };
    return k->register_app(&d) < 0;
}
