#include "kapi.h"
#include "gdi.h"
#include "g3d.h"
#include "saver.h"
#include "saver_core.inc"
#include "cube3d.inc"

static const Kapi *api;
static G3D *ctx;
static G3VtxN cube[CUBE_VERTS];
static u16 cube_idx[CUBE_INDICES];

#define BOX_W 120
#define BOX_H 140

static void draw_floppy(int x, int y, u8 col)
{
    api->fill_rect(x, y, 60, 54, col);
    api->fill_rect(x + 12, y, 30, 20, C_G0 + 2);
    api->fill_rect(x + 30, y + 2, 8, 15, col);
    api->fill_rect(x + 8, y + 26, 44, 24, C_WHITE);
    for (int i = 0; i < 3; i++)
        api->hline(x + 12, y + 32 + i * 5, 36, C_G0 + 3);
    api->bevel(x, y, 60, 54, 0);
}

static void saver_draw(void)
{
    static const u8 cols[6] = { C_BGREEN, C_CYAN, C_YELLOW, C_MAGENTA, C_RED, C_BBLUE };
    u32 t = *api->ticks, step = t / 3, hx, hy;
    int x = sv_bounce(step * 3, *api->screen_w - BOX_W, &hx);
    int y = sv_bounce(step * 2, *api->screen_h - BOX_H, &hy);
    u8 col = cols[(hx + hy) % 6];

    const GdiOps *gd = gdi_bind(api, 10);
    const G3dOps *g3 = g3d_bind(api, 3);
    if (gd && g3 && !ctx) {
        ctx = g3->create();
        if (ctx) cube3d_lit(g3, ctx);
    }
    if (!gd || !g3 || !ctx) {
        draw_floppy(x, y, col);
        api->draw_text(x + 6, y + 58, "FLOPNIX", col);
        return;
    }

    for (int i = 0; i < CUBE_VERTS; i++) cube[i].diffuse = GIDX(col);
    g3->viewport(ctx, x, y, BOX_W, BOX_W, FX(1), FX(16));
    cube3d_draw(g3, ctx, (fx)(((t * 190u) & 0xFFFFu) * 360u),
                (fx)(((t * 121u) & 0xFFFFu) * 360u), cube, cube_idx);
    api->draw_text(x + BOX_W / 2 - 28, y + BOX_W + 4, "FLOPNIX", col);
}

static const SaverOps saver_ops = { SAVER_ABI, saver_draw };

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "saver"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    cube3d_build(cube, cube_idx, 0);
    return api->register_service("saver", &saver_ops) < 0;
}
