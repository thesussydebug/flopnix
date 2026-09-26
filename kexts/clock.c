#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *g;

static const i16 S[16] = { 0, 105, 208, 309, 407, 500, 588, 669,
                           743, 809, 866, 914, 951, 978, 995, 1000 };

static int isin60(int i)
{
    i %= 60;
    if (i < 0) i += 60;
    if (i <= 15) return S[i];
    if (i <= 30) return S[30 - i];
    if (i <= 45) return -S[i - 30];
    return -S[60 - i];
}
#define icos60(i) isin60((i) + 15)

static void hand(int cx, int cy, int i, int len, int t, u8 col)
{
    int dx = isin60(i), dy = -icos60(i);
    for (int r = 0; r <= len; r++)
        api->fill_rect(cx + dx * r / 1000 - t / 2,
                       cy + dy * r / 1000 - t / 2, t, t, col);
}

static void clock_draw(Win *w, int cx, int cy, int cw, int ch)
{
    g = gdi_bind(api, 11);
    (void)w; (void)ch;
    int h, m, s, D, M, Y;
    api->rtc_read(&h, &m, &s, &D, &M, &Y);

    int ox = cx + cw / 2, oy = cy + 78, R = 62;
    if (g) {
        g->set_dither(1);
        g->fill_gradient(cx, cy, cw, ch, GRGB(226, 232, 244), GRGB(180, 192, 216), 1);
        g->set_dither(0);
        GPt ring[30];
        for (int i = 0; i < 30; i++) {
            ring[i].x = ox + R * isin60(i * 2) / 1000;
            ring[i].y = oy - R * icos60(i * 2) / 1000;
        }
        g->fill_poly(ring, 30, GRGB(250, 252, 255));
        g->stroke_poly(ring, 30, 1, 2, GRGB(40, 60, 120));
        for (int i = 0; i < 60; i += 5) {
            int px = ox + (R - 7) * isin60(i) / 1000;
            int py = oy - (R - 7) * icos60(i) / 1000;
            g->fill(px - 1, py - 1, 3, 3, 0);
        }
        GPt hh[2] = { { ox, oy }, { ox + (R - 30) * isin60((h % 12) * 5 + m / 12) / 1000,
                                    oy - (R - 30) * icos60((h % 12) * 5 + m / 12) / 1000 } };
        g->stroke_poly(hh, 2, 0, 5, GRGB(30, 40, 80));
        GPt mh[2] = { { ox, oy }, { ox + (R - 16) * isin60(m) / 1000,
                                    oy - (R - 16) * icos60(m) / 1000 } };
        g->stroke_poly(mh, 2, 0, 3, GRGB(30, 40, 80));
        g->draw_line(ox, oy, ox + (R - 11) * isin60(s) / 1000,
                     oy - (R - 11) * icos60(s) / 1000, GRGB(210, 50, 50));
        g->fill_rgb(ox - 2, oy - 2, 5, 5, GRGB(30, 40, 80));
    } else {
        api->fill_rect(ox - R - 6, oy - R - 6, 2 * R + 12, 2 * R + 12, C_FACE);
        for (int i = 0; i < 60; i++) {
            int px = ox + R * isin60(i) / 1000, py = oy - R * icos60(i) / 1000;
            if (i % 5 == 0) api->fill_rect(px - 2, py - 2, 4, 4, C_NAVY);
            else api->fill_rect(px, py, 1, 1, C_G0 + 3);
        }
        hand(ox, oy, (h % 12) * 5 + m / 12, R - 28, 3, C_BLACK);
        hand(ox, oy, m, R - 14, 2, C_BLACK);
        hand(ox, oy, s, R - 9, 1, C_RED);
        api->fill_rect(ox - 2, oy - 2, 5, 5, C_NAVY);
    }

    char t[24];
    api->kfmt(t, sizeof t, "%02d:%02d:%02d", h, m, s);
    api->draw_text(ox - 4 * 8, cy + 152, t, C_NAVY);
    api->kfmt(t, sizeof t, "%04d-%02d-%02d", Y, M, D);
    api->draw_text(ox - 5 * 8, cy + 168, t, C_G0 + 3);
}

static void clock_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 168;
    *h = 190;
}

static int my_type = -1, timer_id = -1, shown_s = -1;

static void clock_tick(void *ctx)
{
    (void)ctx;
    int h, m, s, D, M, Y;
    api->rtc_read(&h, &m, &s, &D, &M, &Y);
    if (s != shown_s) { shown_s = s; api->win_redraw(my_type, 0); }
}

static void clock_open(int inst)
{
    (void)inst;
    if (timer_id < 0) timer_id = api->timer_add(25, clock_tick, 0);
}

static void clock_close(int inst)
{
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Clock"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    g = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Clock", .max_inst = 1, .in_menu = 1,
        .open = clock_open, .close = clock_close,
        .draw = clock_draw, .client_size = clock_csize,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
