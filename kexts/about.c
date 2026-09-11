#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *g;

static void about_draw(Win *w, int cx, int cy, int cw, int ch)
{
    g = gdi_bind(api, 11);
    (void)w; (void)ch;
    char v[40];
    int fx = cx + 14, fy = cy + 14;
    if (g) {
        g->set_dither(1);
        g->fill_gradient(cx, cy, cw, 62, GRGB(70, 120, 200), GRGB(18, 42, 96), 1);
        g->set_dither(0);
    }
    api->fill_rect(fx, fy, 40, 40, C_NAVY);
    api->fill_rect(fx + 10, fy, 20, 14, C_SILVER);
    api->fill_rect(fx + 20, fy + 2, 6, 10, C_NAVY);
    api->fill_rect(fx + 6, fy + 20, 28, 20, C_WHITE);
    api->kfmt(v, sizeof v, "FLOPNIX %s", api->os_version);
    api->draw_text(cx + 66, cy + 14, v, C_WHITE);
    api->draw_text(cx + 66, cy + 32, "by tar0byte", C_BGREEN);
    api->kfmt(v, sizeof v, "(c) 2026 - built %s", api->os_build_date);
    api->draw_text(cx + 66, cy + 48, v, C_SILVER);
    api->draw_text(cx + 14, cy + 74, "a Unix-like OS written in C", C_BLACK);
    api->draw_text(cx + 14, cy + 90, "for floppy disk nerds like", C_BLACK);
    api->draw_text(cx + 14, cy + 106, "yourself.", C_BLACK);
    api->draw_text(cx + 14, cy + 122, "now with kernel extensions", C_G0 + 4);
}

static void about_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 336;
    *h = 152;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "About"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    g = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "About", .max_inst = 1, .in_menu = 1,
        .draw = about_draw, .client_size = about_csize,
    };
    return k->register_app(&d) < 0;
}
