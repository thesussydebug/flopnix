#include "kapi.h"
#include "gdi.h"
#include "g3d.h"
#include "ui.inc"

static const Kapi *api;
static const GdiOps *g;
static const G3dOps *g3;

static const GdiOps *demo_gdi(void)
{
    const GdiOps *d = gdi_bind(api, GDI_ABI);
    return d && d->fill_rgb && d->set_dither && d->fill_gradient &&
        d->fill_poly_aa && d->fill_poly && d->stroke_poly && d->rgn_rect &&
        d->rgn_combine && d->fill_rgn && d->rgn_free ? d : 0;
}
static const G3dOps *demo_g3d(void)
{
    const G3dOps *d = g3d_bind(api, 3);
    return d && d->create && d->destroy && d->matrix_mode && d->mat_persp &&
        d->load && d->mat_trs && d->viewport && d->clear && d->draw &&
        d->light && d->set && d->clearz ? d : 0;
}

static G3D *ctx3;
static int demo_type = -1, timer_id = -1;
static int page, paused;
static u32 spin;

#define WCOL GRGB(210, 235, 250)
static const G3Vtx wirev[8] = {
    { -FX(1), -FX(1), -FX(1), WCOL }, {  FX(1), -FX(1), -FX(1), WCOL },
    {  FX(1),  FX(1), -FX(1), WCOL }, { -FX(1),  FX(1), -FX(1), WCOL },
    { -FX(1), -FX(1),  FX(1), WCOL }, {  FX(1), -FX(1),  FX(1), WCOL },
    {  FX(1),  FX(1),  FX(1), WCOL }, { -FX(1),  FX(1),  FX(1), WCOL },
};
static const u16 wiree[24] = {
    0,1, 1,2, 2,3, 3,0,  4,5, 5,6, 6,7, 7,4,  0,4, 1,5, 2,6, 3,7
};

#define QUAD(a,b,c,d, nx,ny,nz, col) \
    { a, nx,ny,nz, col }, { b, nx,ny,nz, col }, \
    { c, nx,ny,nz, col }, { d, nx,ny,nz, col }
#define P  FX(1)
#define N -FX(1)
#define V(x,y,z) x, y, z
static const G3VtxN litv[24] = {
    QUAD(V(N,N,P), V(P,N,P), V(P,P,P), V(N,P,P), 0,0,P, GRGB(205, 80, 80)),
    QUAD(V(P,N,N), V(N,N,N), V(N,P,N), V(P,P,N), 0,0,N, GRGB(215, 150, 70)),
    QUAD(V(P,N,P), V(P,N,N), V(P,P,N), V(P,P,P), P,0,0, GRGB(90, 190, 100)),
    QUAD(V(N,N,N), V(N,N,P), V(N,P,P), V(N,P,N), N,0,0, GRGB(80, 190, 185)),
    QUAD(V(N,P,P), V(P,P,P), V(P,P,N), V(N,P,N), 0,P,0, GRGB(95, 130, 220)),
    QUAD(V(N,N,N), V(P,N,N), V(P,N,P), V(N,N,P), 0,N,0, GRGB(170, 105, 205)),
};
#undef QUAD
#undef P
#undef N
#undef V
static const u16 solidi[36] = {
    0,1,2, 0,2,3,     4,5,6, 4,6,7,     8,9,10, 8,10,11,
    12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23
};

static int demo_is_open(void)
{
    for (int i = 0; i < api->win_max(); i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == demo_type) return 1;
    }
    return 0;
}

static void dtick(void *c)
{
    (void)c;
    if (!demo_is_open() || page != 1 || paused) return;
    spin += 560;
    api->gui_dirty();
}

static void demo_timer(void) {
    if (page == 1 && !paused && g && g3 && ctx3) {
        if (timer_id < 0) timer_id = api->timer_add(3, dtick, 0);
    } else if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}
static void demo_open(int inst) { (void)inst; demo_timer(); }
static void demo_mouse(int inst, int lx, int ly, int ev, int cw, int ch) {
    (void)inst; (void)ch;
    ly += 24;
    if (ev != EV_PRESS || ly < 32 || ly >= 56) return;
    if (lx >= 8 && lx < 140) page = 0;
    else if (lx >= 146 && lx < 278) page = 1;
    else if (page && g && g3 && lx >= cw - 100 && lx < cw - 8) paused = !paused;
    demo_timer(); api->gui_dirty();
}

static void demo_close(int inst)
{
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
    g3 = demo_g3d();
    if (ctx3 && g3) g3->destroy(ctx3);
    ctx3 = 0;
}

static void demo_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    cy -= 24; ch += 24;
    g = demo_gdi();
    g3 = demo_g3d();
    demo_timer();
    ui_button(cx, cy, ui_r(8,32,132,24), "2D shapes", !page, 1);
    ui_button(cx, cy, ui_r(146,32,132,24), "3D animation", page, 1);
    if (page && g && g3) ui_button(cx, cy, ui_r(cw-100,32,92,24), paused ? "Resume" : "Pause", 0, 1);
    if (!g) {
        api->draw_text_clip(cx+12, cy+74, "gdi.kx or both graphics APIs are missing", C_MAROON, cw-24);
        api->draw_text_clip(cx+12, cy+90, "or don't support required functions.", C_MAROON, cw-24);
        api->draw_text(cx+12, cy+106, "please fix!", C_MAROON);
        return;
    }
    g->fill_rgb(cx+8, cy+64, cw-16, ch-72, GRGB(32,32,56));
    if (!page) {
        api->draw_text(cx+20, cy+76, "Blending colours", C_WHITE);
        g->set_dither(0);
        g->fill_gradient(cx+20,cy+100,cw-40,12,GRGB(40,80,220),GRGB(230,90,80),0);
        g->set_dither(1);
        g->fill_gradient(cx+20,cy+116,cw-40,12,GRGB(40,80,220),GRGB(230,90,80),0);
        g->set_dither(0);
        api->draw_text_clip(cx+20,cy+138,"Above: colour steps. Below: a smoother blend.",C_G0+6,cw-40);
        api->draw_text(cx+20,cy+172,"Shapes, curves and cutouts",C_WHITE);
        int step=(cw-40)/4, x=cx+20, y=cy+204;
        GPt tri[3]={{x,y+40},{x+48,y+40},{x+24,y}};
        g->fill_poly_aa(tri,3,GRGB(240,150,70));
        x+=step;
        GPt star[5]={{x+24,y},{x+38,y+42},{x,y+16},{x+48,y+16},{x+10,y+42}};
        g->fill_poly(star,5,GRGB(90,200,220));
        x+=step;
        GPt zig[4]={{x,y+38},{x+16,y},{x+32,y+38},{x+48,y}};
        g->stroke_poly(zig,4,0,6,GRGB(220,160,240));
        x+=step;
        GRgn *outer=g->rgn_rect(x,y,x+48,y+42), *hole=g->rgn_rect(x+12,y+10,x+36,y+32);
        if (outer && hole) { g->rgn_combine(outer,outer,hole,RGN_DIFF); g->fill_rgn(outer,GRGB(100,215,130)); }
        g->rgn_free(outer); g->rgn_free(hole);
        api->draw_text_clip(cx+20,cy+264,"Smooth edges, filled shapes and rounded lines.",C_G0+6,cw-40);
        return;
    }
    if (paused) api->draw_text(cx+20,cy+76,"Rotation paused",C_WHITE);
    if (!g3) {
        api->draw_text_clip(cx+20,cy+94,"g3d.kx is missing, or doesn't support",C_G0+6,cw-40);
        api->draw_text(cx+20,cy+110,"required functions.",C_G0+6);
        return;
    }
    if (g3 && !ctx3) ctx3 = g3->create();
    demo_timer();
    if (!ctx3) { api->draw_text(cx+20,cy+110,"Not enough memory for a 3D preview.",C_G0+6); return; }
    int p3y = cy+112;
            GMat pm, wm;
            GVec t = { 0, 0, -FX(5), 0 };
            GVec r = { (fx)((spin & 0xFFFFu) * 360u),
                       (fx)(((spin * 2u / 3u) & 0xFFFFu) * 360u), 0, 0 };
            g3->matrix_mode(ctx3, G3M_PROJ);
            g3->mat_persp(&pm, FX(50), fx_div(FX(130), FX(130)), FX(1), FX(16));
            g3->load(ctx3, &pm);
            g3->matrix_mode(ctx3, G3M_WORLD);
            g3->mat_trs(&wm, &t, &r, 0);
            g3->load(ctx3, &wm);

            g3->viewport(ctx3, cx + 28, p3y, 130, 130, FX(1), FX(16));
            g3->clear(ctx3, GRGB(14, 16, 30));
            g3->draw(ctx3, G3P_LINES, G3F_XYZ | G3F_DIFFUSE, wirev, 8, wiree, 24);

            g3->viewport(ctx3, cx + cw - 158, p3y, 130, 130, FX(1), FX(16));
            g3->clear(ctx3, GRGB(14, 16, 30));
            G3Light sun = { { -30000, -40000, -45000, 0 }, FX_ONE };
            g3->light(ctx3, 0, &sun);
            g3->set(ctx3, G3S_LIGHTING, 1);
            g3->set(ctx3, G3S_AMBIENT, (u32)(FX_ONE / 4));
            g3->set(ctx3, G3S_SHADE, G3SHADE_GOURAUD);
            g3->set(ctx3, G3S_ZENABLE, 1);
            g3->clearz(ctx3, FX_ONE);
            g3->draw(ctx3, G3P_TRIS, G3F_XYZ | G3F_NORMAL | G3F_DIFFUSE,
                     litv, 24, solidi, 36);
            g3->set(ctx3, G3S_ZENABLE, 0);
            g3->set(ctx3, G3S_LIGHTING, 0);
            g3->set(ctx3, G3S_SHADE, G3SHADE_FLAT);

    api->draw_text(cx+28,cy+254,"Outline",C_WHITE);
    api->draw_text(cx+cw-158,cy+254,"Light and shade",C_WHITE);
    api->draw_text_clip(cx+20,cy+284,"Lighting helps a flat screen show depth.",C_G0+6,cw-40);
}

static void demo_csize(int inst, int *w, int *h) { (void)inst; *w = 480; *h = 330; }
static void demo_minsize(int *w, int *h) { *w = 390; *h = 310; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "GFX Demo"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    g = gdi_bind(k, GDI_ABI); ui_init(k, g);
    static const AppDesc d = {
        .title = "GFX Demo", .max_inst = 1, .resizable = 1, .in_menu = 1,
        .category = APP_CAT_DEV,
        .open = demo_open, .draw = demo_draw, .close = demo_close, .mouse = demo_mouse,
        .client_size = demo_csize, .min_client = demo_minsize,
    };
    demo_type = k->register_app(&d);
    return demo_type < 0;
}
