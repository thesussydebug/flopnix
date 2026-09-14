/* Transforms, clips, and draws 3D geometry. */
#include "kapi.h"
#include "gdi.h"
#include "g3d.h"
#include "g3d_math.inc"

static const Kapi *api;
static const GdiOps *gd;

static const GdiOps *gdi(void)
{
    gd = gdi_bind(api, 10);
    return gd;
}

void *memcpy(void *d, const void *s, u32 n)
{
    u8 *dd = d; const u8 *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, u32 n)
{
    u8 *dd = d;
    while (n--) *dd++ = (u8)c;
    return d;
}
void *memmove(void *d, const void *s, u32 n)
{
    u8 *dd = d; const u8 *ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
int memcmp(const void *a, const void *b, u32 n)
{
    const u8 *aa = a, *bb = b;
    while (n--) { if (*aa != *bb) return (int)*aa - (int)*bb; aa++; bb++; }
    return 0;
}

#define G3_STK 8
struct G3D {
    GMat stk[3][G3_STK];
    u8   top[3], mode;
    int  vx, vy, vw, vh;
    fx   zn, zf;
    u32  st[G3S_MAX];
    G3Stats stats;
    u16 *zbuf;
    int  zbw, zbh;
    G3Light lights[8];
    u8   lit[8];
    G3Mtl mtl;
};

static u8 *cmap;
static u8  cmap_tried;
static void cmap_ensure(void)
{
    if (cmap_tried || !gdi()) return;
    cmap_tried = 1;
    if (gd->abi >= 11) gd->pal_ramps();
    cmap = (u8 *)api->kmalloc(64 * 256);
    if (!cmap) return;
    for (int i = 0; i < 256; i++) {
        u32 base = gd->pal_unpack((u8)i);
        for (int l = 0; l < 64; l++)
            cmap[l * 256 + i] = gd->pal_index(g3_shade_rgb(base, l));
    }
}

static void zbuf_ensure(G3D *c)
{
    if (c->zbuf && c->zbw == c->vw && c->zbh == c->vh) return;
    if (c->zbuf) { api->kfree(c->zbuf); c->zbuf = 0; }
    if (c->vw <= 0 || c->vh <= 0) return;
    if ((u32)c->vw > 0x7FFFFFFFu / 2 / (u32)c->vh) return;
    u32 pixels = (u32)c->vw * (u32)c->vh;
    c->zbuf = (u16 *)api->kmalloc(pixels * 2);
    if (c->zbuf) {
        c->zbw = c->vw; c->zbh = c->vh;
        for (u32 i = 0; i < pixels; i++) c->zbuf[i] = 0xFFFF;
    }
}

static GMat *cur(G3D *c) { return &c->stk[c->mode][c->top[c->mode]]; }

static G3D *g3_create(void)
{
    if (!gdi()) return 0;
    G3D *c = (G3D *)api->kmalloc(sizeof *c);
    if (!c) return 0;
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 3; i++) m4_ident(c->stk[i][0].m);
    c->zn = FX(1); c->zf = FX(16);
    c->st[G3S_FILL]  = G3FILL_SOLID;
    c->st[G3S_SHADE] = G3SHADE_FLAT;
    c->st[G3S_CULL]  = G3CULL_NONE;
    c->st[G3S_ZWRITE] = 1;
    c->mtl.ambient = FX_ONE;
    c->mtl.diffuse = FX_ONE;
    return c;
}

static int g3_light_set(G3D *c, int i, const G3Light *l)
{
    if (!c || i < 0 || i > 7) return -1;
    if (!l) { c->lit[i] = 0; return 0; }
    c->lights[i] = *l;
    g3_norm3(&c->lights[i].dir);
    c->lit[i] = 1;
    return 0;
}
static void g3_material_set(G3D *c, const G3Mtl *m) { if (c && m) c->mtl = *m; }
static void g3_mat_lookat(GMat *m, const GVec *eye, const GVec *at, const GVec *up)
{ if (m && eye && at && up) m4_lookat(m->m, eye, at, up); }
static void g3_destroy(G3D *c)
{
    if (!c) return;
    if (c->zbuf) api->kfree(c->zbuf);
    api->kfree(c);
}

static void g3_clearz(G3D *c, fx z)
{
    if (!c) return;
    zbuf_ensure(c);
    if (!c->zbuf) return;
    u16 d = (u16)g3_zmap16(z);
    for (int i = 0; i < c->zbw * c->zbh; i++) c->zbuf[i] = d;
}

static void g3_viewport(G3D *c, int x, int y, int w, int h, fx zn, fx zf)
{
    if (!c) return;
    c->vx = x; c->vy = y; c->vw = w; c->vh = h; c->zn = zn; c->zf = zf;
}
static void g3_clear(G3D *c, GRGB col)
{
    if (c && gdi()) gd->fill_rgb(c->vx, c->vy, c->vw, c->vh, col);
}

static void g3_matrix_mode(G3D *c, int m) { if (c && m >= 0 && m < 3) c->mode = (u8)m; }
static void g3_identity(G3D *c) { if (c) m4_ident(cur(c)->m); }
static void g3_load(G3D *c, const GMat *m) { if (c && m) *cur(c) = *m; }
static void g3_mult(G3D *c, const GMat *m)
{
    if (c && m) m4_mul(cur(c)->m, cur(c)->m, m->m);
}
static void g3_push(G3D *c)
{
    if (!c) return;
    u8 *t = &c->top[c->mode];
    if (*t + 1 < G3_STK) { c->stk[c->mode][*t + 1] = c->stk[c->mode][*t]; (*t)++; }
}
static void g3_pop(G3D *c)
{
    if (c && c->top[c->mode]) c->top[c->mode]--;
}

static void g3_set(G3D *c, int s, u32 v) { if (c && s >= 0 && s < G3S_MAX) c->st[s] = v; }
static u32  g3_get(G3D *c, int s) { return (c && s >= 0 && s < G3S_MAX) ? c->st[s] : 0; }

static void g3_mvp(G3D *c, fx *mvp)
{
    fx t[16];
    m4_mul(t, c->stk[G3M_VIEW][c->top[G3M_VIEW]].m,
              c->stk[G3M_WORLD][c->top[G3M_WORLD]].m);
    m4_mul(mvp, c->stk[G3M_PROJ][c->top[G3M_PROJ]].m, t);
}

static int g3_draw(G3D *c, int prim, u32 fvf, const void *v, int nv,
                   const u16 *idx, int ni)
{
    if (!c || !gdi() || !v || nv <= 0) return 0;
    if (fvf != (G3F_XYZ | G3F_DIFFUSE) &&
        fvf != (G3F_XYZ | G3F_NORMAL | G3F_DIFFUSE)) return 0;
    const u8 *vb = (const u8 *)v;
    u32 stride = (fvf & G3F_NORMAL) ? sizeof(G3VtxN) : sizeof(G3Vtx);
    u32 dofs = stride - 4;
#define VP(k) ((const fx *)(vb + (u32)(k) * stride))
#define VD(k) (*(const u32 *)(vb + (u32)(k) * stride + dofs))

    G3Light la[8];
    int nl = 0;
    int lighting = c->st[G3S_LIGHTING] && (fvf & G3F_NORMAL);
    if (lighting)
        for (int i = 0; i < 8; i++) if (c->lit[i]) la[nl++] = c->lights[i];
    fx amb = (fx)c->st[G3S_AMBIENT];
    const fx *W = c->stk[G3M_WORLD][c->top[G3M_WORLD]].m;
    u32 t0h, t0l, t1h, t1l;
    api->tsc_read(&t0h, &t0l);
    fx mvp[16];
    g3_mvp(c, mvp);
    int n = idx ? ni : nv, drawn = 0;
#define VIDX(k) (int)(idx ? idx[k] : (u16)(k))

    if (prim == G3P_TRIS || prim == G3P_TRISTRIP || prim == G3P_TRIFAN) {
        int nt = prim == G3P_TRIS ? n / 3 : n - 2;

        int own = c->st[G3S_ZENABLE] || c->st[G3S_SHADE] == G3SHADE_GOURAUD;
        u8 *sp = 0; int pitch = 0, pw = 0, ph = 0, lx = 0, ly = 0, hx = 0, hy = 0;
        if (own && c->st[G3S_FILL] == G3FILL_SOLID) {
            if (c->st[G3S_ZENABLE]) zbuf_ensure(c);
            if (c->st[G3S_SHADE] == G3SHADE_GOURAUD) cmap_ensure();
            if (api->surface_lock(&sp, &pitch, &pw, &ph)) {
                int cx2, cy2, cw2, ch2;
                api->clip_rect_get(&cx2, &cy2, &cw2, &ch2);
                lx = cx2 < 0 ? 0 : cx2; ly = cy2 < 0 ? 0 : cy2;
                hx = cx2 + cw2 > pw ? pw : cx2 + cw2;
                hy = cy2 + ch2 > ph ? ph : cy2 + ch2;
            } else own = 0;
        } else own = 0;

        for (int k = 0; k < nt; k++) {
            c->stats.in++;
            int ix[3];
            g3_prim_tri(prim, k, &ix[0], &ix[1], &ix[2]);
            GVec cs[16]; fx at[16];
            int bad = 0;
            for (int j = 0; j < 3; j++) {
                int vi = VIDX(ix[j]);
                if (vi >= nv) { bad = 1; break; }
                const fx *P = VP(vi);
                GVec vv = { P[0], P[1], P[2], FX_ONE };
                m4_vec(&cs[j], mvp, &vv);
                if (lighting) {
                    GVec nm = { P[3], P[4], P[5], 0 }, nw;
                    m4_vec(&nw, W, &nm);
                    g3_norm3(&nw);
                    at[j] = FX(g3_light_level(&nw, la, nl, amb,
                                              c->mtl.ambient, c->mtl.diffuse));
                } else
                    at[j] = FX(g3_lum6(VD(vi)));
            }
            if (bad) continue;
            int m = g3_clip_polya(cs, at, 3);
            if (m < 3) { c->stats.clipped++; continue; }
            G3SV sv[16];
            GPt pts[16];
            for (int j = 0; j < m; j++) {
                GVec s;
                g3_project_vp(&s, &cs[j], c->vx, c->vy, c->vw, c->vh);
                sv[j].x = s.x; sv[j].y = s.y; sv[j].z = s.z; sv[j].l = at[j];
                pts[j].x = (s.x + 32768) >> 16;
                pts[j].y = (s.y + 32768) >> 16;
            }
            i32 area2 = 0;
            for (int j = 0; j < m; j++) {
                const GPt *p0 = &pts[j], *p1 = &pts[(j + 1) % m];
                area2 += p0->x * p1->y - p1->x * p0->y;
            }
            u32 cull = c->st[G3S_CULL];
            if ((cull == G3CULL_CW  && area2 > 0) ||
                (cull == G3CULL_CCW && area2 < 0)) { c->stats.culled++; continue; }
            c->stats.drawn++; drawn++;
            u32 col = VD(VIDX(ix[0]));
            if (c->st[G3S_FILL] != G3FILL_SOLID) {
                for (int j = 0; j < m; j++)
                    c->stats.pixels += (u32)gd->draw_line(pts[j].x, pts[j].y,
                        pts[(j + 1) % m].x, pts[(j + 1) % m].y, col);
                continue;
            }
            if (!own) {
                c->stats.pixels += (u32)gd->fill_poly(pts, m, col);
                c->stats.spans += (u32)m;
                continue;
            }

            u8 base = gd->pal_index(col);
            int gouraud = c->st[G3S_SHADE] == G3SHADE_GOURAUD && cmap != 0;
            int zon = c->st[G3S_ZENABLE] && c->zbuf != 0;
            int zwr = c->st[G3S_ZWRITE] != 0;
            for (int t2 = 1; t2 + 1 < m; t2++) {
                const G3SV *A = &sv[0], *B = &sv[t2], *C = &sv[t2 + 1];
                fx yminf = A->y < B->y ? A->y : B->y;
                fx ymaxf = A->y > B->y ? A->y : B->y;
                if (C->y < yminf) yminf = C->y;
                if (C->y > ymaxf) ymaxf = C->y;
                int y0 = (yminf >> 16) - 1, y1 = (ymaxf >> 16) + 2;
                if (y0 < ly) y0 = ly;
                if (y1 > hy) y1 = hy;
                for (int y = y0; y < y1; y++) {
                    G3SV L, R;
                    if (!tri_scan(A, B, C, y, &L, &R)) continue;
                    int x0 = fx_ceil(L.x - 32768), x1 = fx_ceil(R.x - 32768);
                    if (x0 < lx) x0 = lx;
                    if (x1 > hx) x1 = hx;
                    if (x1 <= x0) continue;
                    fx span = R.x - L.x;
                    fx dz = span > 64 ? fx_div(R.z - L.z, span) : 0;
                    fx dl = span > 64 ? fx_div(R.l - L.l, span) : 0;
                    fx ofs = FX(x0) + 32768 - L.x;
                    fx z = L.z + fx_mul(dz, ofs);
                    fx lgt = L.l + fx_mul(dl, ofs);
                    u8 *row = sp + y * pitch;
                    c->stats.spans++;
                    for (int x = x0; x < x1; x++, z += dz, lgt += dl) {
                        if (zon &&
                            (u32)(x - c->vx) < (u32)c->zbw &&
                            (u32)(y - c->vy) < (u32)c->zbh) {
                            u16 d = (u16)g3_zmap16(z);
                            u16 *zp = &c->zbuf[(y - c->vy) * c->zbw + (x - c->vx)];
                            if (d >= *zp) continue;
                            if (zwr) *zp = d;
                        }
                        int lv = lgt >> 16;
                        if (lv < 0) lv = 0; else if (lv > 63) lv = 63;
                        row[x] = gouraud ? cmap[lv * 256 + base] : base;
                        c->stats.pixels++;
                    }
                }
            }
        }
        if (sp) api->surface_unlock();
    } else if (prim == G3P_LINES) {
        for (int i = 0; i + 1 < n; i += 2) {
            c->stats.in++;
            int k0 = VIDX(i), k1 = VIDX(i + 1);
            if (k0 >= nv || k1 >= nv) continue;
            GVec a, b, vv;
            { const fx *P0 = VP(k0); vv = (GVec){ P0[0], P0[1], P0[2], FX_ONE }; }; m4_vec(&a, mvp, &vv);
            { const fx *P1 = VP(k1); vv = (GVec){ P1[0], P1[1], P1[2], FX_ONE }; }; m4_vec(&b, mvp, &vv);
            if (!g3_clip_seg(&a, &b)) { c->stats.clipped++; continue; }
            GVec sa, sb;
            g3_project_vp(&sa, &a, c->vx, c->vy, c->vw, c->vh);
            g3_project_vp(&sb, &b, c->vx, c->vy, c->vw, c->vh);
            c->stats.drawn++; drawn++;
            c->stats.pixels += (u32)gd->draw_line(
                (sa.x + 32768) >> 16, (sa.y + 32768) >> 16,
                (sb.x + 32768) >> 16, (sb.y + 32768) >> 16, VD(k0));
        }
    }
#undef VIDX
    api->tsc_read(&t1h, &t1l);
    u32 mhz = api->cpu_mhz();
    if (mhz) c->stats.us += (t1l - t0l) / mhz;
    return drawn;
}

static int g3_project(G3D *c, const GVec *in, int n, GVec *out)
{
    if (!c || !in || !out || n <= 0) return 0;
    fx mvp[16];
    g3_mvp(c, mvp);
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        GVec vv = { in[i].x, in[i].y, in[i].z, FX_ONE }, cs;
        m4_vec(&cs, mvp, &vv);
        if (cs.w < G3_WEPS) out[i] = (GVec){ 0, 0, 0, 0 };
        else { g3_project_vp(&out[i], &cs, c->vx, c->vy, c->vw, c->vh); cnt++; }
    }
    return cnt;
}

static int g3_stats(G3D *c, G3Stats *s)
{
    if (!c || !s) return -1;
    *s = c->stats;
    return 0;
}

static void g3_mat_mul(GMat *o, const GMat *a, const GMat *b)
{ if (o && a && b) m4_mul(o->m, a->m, b->m); }
static void g3_mat_persp(GMat *m, fx fovy_deg, fx aspect, fx zn, fx zf)
{ if (m) m4_persp(m->m, fovy_deg, aspect, zn, zf); }
static void g3_mat_rot_xyz(GMat *m, fx ax, fx ay, fx az)
{
    if (!m) return;
    fx rx[16], ry[16], rz[16], t[16];
    m4_rot_x(rx, g3_deg2brad(ax));
    m4_rot_y(ry, g3_deg2brad(ay));
    m4_rot_z(rz, g3_deg2brad(az));
    m4_mul(t, ry, rx);
    m4_mul(m->m, rz, t);
}
static void g3_mat_trs(GMat *m, const GVec *t, const GVec *r_deg, const GVec *s)
{
    if (!m) return;
    fx tm[16], w[16];
    m4_ident(m->m);
    if (s) {
        m4_ident(w);
        w[0] = s->x; w[5] = s->y; w[10] = s->z;
        m4_mul(m->m, w, m->m);
    }
    if (r_deg) {
        GMat rm;
        g3_mat_rot_xyz(&rm, r_deg->x, r_deg->y, r_deg->z);
        m4_mul(m->m, rm.m, m->m);
    }
    if (t) {
        m4_trans(tm, t->x, t->y, t->z);
        m4_mul(m->m, tm, m->m);
    }
}

static const G3dOps g3d_ops = {
    .abi = G3D_ABI,
    .create = g3_create, .destroy = g3_destroy,
    .viewport = g3_viewport, .clear = g3_clear,
    .matrix_mode = g3_matrix_mode, .identity = g3_identity,
    .load = g3_load, .mult = g3_mult, .push = g3_push, .pop = g3_pop,
    .set = g3_set, .get = g3_get,
    .draw = g3_draw, .project = g3_project, .stats = g3_stats,
    .mat_mul = g3_mat_mul, .mat_persp = g3_mat_persp,
    .mat_rot_xyz = g3_mat_rot_xyz, .mat_trs = g3_mat_trs,
    .clearz = g3_clearz,
    .light = g3_light_set, .material = g3_material_set,
    .mat_lookat = g3_mat_lookat,
};

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "g3d"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    return api->register_service("g3d", &g3d_ops) < 0;
}
