/* Provides 2D shapes, paths, and shaded drawing. */
#include "kapi.h"
#include "gdi.h"
#include "gdi_rgn.inc"
#include "gdi_poly.inc"
#include "gdi_curve.inc"
#include "gdi_stroke.inc"
#include "gdi_aa.inc"
#include "gdi_line.inc"

static const Kapi *api;

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

static u32 rgb_of[256];
static u8 *lut;
static u32 lut_us;
static u8  lut_tried;
static u8  dither_on;

static int pal_build(void)
{

    static u8 ramps_done;
    if (!ramps_done) {
        ramps_done = 1;
        for (int r = 0; r < GDI_RAMPS; r++)
            for (int s = 0; s < GDI_RAMP_N; s++) {
                u32 c = gdi_ramp_entry(r, s);
                api->palette_set(GDI_RAMP_BASE + r * GDI_RAMP_N + s,
                                 (u8)(c >> 16), (u8)(c >> 8), (u8)c);
            }
    }
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        api->palette_rgb(i, &r, &g, &b);
        rgb_of[i] = ((u32)r << 16) | ((u32)g << 8) | b;
    }
    lut_tried = 1;
    if (!lut) lut = (u8 *)api->kmalloc(32768);
    if (!lut) { lut_us = 0; return 0; }

    u32 t0h, t0l, t1h, t1l;
    api->tsc_read(&t0h, &t0l);
    int k = 0;
    for (int r = 0; r < 32; r++)
        for (int g = 0; g < 32; g++)
            for (int b = 0; b < 32; b++)
                lut[k++] = gdi_nearest((r << 3) | (r >> 2), (g << 3) | (g >> 2),
                                       (b << 3) | (b >> 2), rgb_of, 256);
    api->tsc_read(&t1h, &t1l);
    u32 mhz = api->cpu_mhz();
    lut_us = mhz ? (t1l - t0l) / mhz : 0;
    return 1;
}

static void ensure_lut(void) { if (!lut_tried) pal_build(); }

static u8 map_rgb(int r, int g, int b)
{
    if (lut) return lut[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
    return api->palette_nearest((u8)r, (u8)g, (u8)b);
}

static u8 map_rgb_dith(int r, int g, int b, int x, int y)
{
    int p = (gdi_bayer(x, y) - 8) * 5;
    r += p; if (r < 0) r = 0; else if (r > 255) r = 255;
    g += p; if (g < 0) g = 0; else if (g > 255) g = 255;
    b += p; if (b < 0) b = 0; else if (b > 255) b = 255;
    return map_rgb(r, g, b);
}
static void g_set_dither(int on) { dither_on = on ? 1 : 0; }

static u8 pal_index(GRGB c)
{
    if (c & 0xFF000000u) return (u8)(c & 0xFF);
    ensure_lut();
    return map_rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static u32 pal_unpack(u8 idx) { ensure_lut(); return rgb_of[idx]; }

static int clipr(int x, int y, int w, int h, u8 **px, int *pitch,
                 int *ox, int *oy, int *ow, int *oh, int *dx, int *dy)
{
    int pw, ph;
    if (!api->surface_lock(px, pitch, &pw, &ph)) return 0;
    int cx, cy, cw, ch;
    api->clip_rect_get(&cx, &cy, &cw, &ch);
    int lx = cx, ly = cy, hx = cx + cw, hy = cy + ch;
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (hx > pw) hx = pw;
    if (hy > ph) hy = ph;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < lx) x0 = lx;
    if (y0 < ly) y0 = ly;
    if (x1 > hx) x1 = hx;
    if (y1 > hy) y1 = hy;
    if (x1 <= x0 || y1 <= y0) return 0;
    *dx = x0 - x; *dy = y0 - y;
    *ox = x0; *oy = y0; *ow = x1 - x0; *oh = y1 - y0;
    return 1;
}

static int g_fill(int x, int y, int w, int h, u8 idx)
{
    u8 *px; int pitch, ox, oy, ow, oh, dx, dy;
    if (!clipr(x, y, w, h, &px, &pitch, &ox, &oy, &ow, &oh, &dx, &dy)) return 0;
    for (int j = 0; j < oh; j++) memset(px + (oy + j) * pitch + ox, idx, ow);
    api->surface_unlock();
    return ow * oh;
}

static int g_fill_rgb(int x, int y, int w, int h, GRGB c)
{
    if (!dither_on || (c & 0xFF000000u))
        return g_fill(x, y, w, h, pal_index(c));
    u8 *px; int pitch, ox, oy, ow, oh, dx, dy;
    if (!clipr(x, y, w, h, &px, &pitch, &ox, &oy, &ow, &oh, &dx, &dy)) return 0;
    ensure_lut();
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    for (int j = 0; j < oh; j++) {
        u8 *row = px + (oy + j) * pitch + ox;
        for (int i = 0; i < ow; i++) row[i] = map_rgb_dith(r, g, b, ox + i, oy + j);
    }
    api->surface_unlock();
    return ow * oh;
}

static int g_fill_gradient(int x, int y, int w, int h, GRGB a, GRGB b, int vertical)
{
    u8 *px; int pitch, ox, oy, ow, oh, dx, dy;
    if (!clipr(x, y, w, h, &px, &pitch, &ox, &oy, &ow, &oh, &dx, &dy)) return 0;
    ensure_lut();
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int spanv = h > 1 ? h - 1 : 1, spanh = w > 1 ? w - 1 : 1;
    if (dither_on && vertical) {

        for (int j = 0; j < oh; j++) {
            u8 *row = px + (oy + j) * pitch + ox;
            int t = ((dy + j) << 8) / spanv;
            if (t > 256) t = 256;
            int r = ar + (((br - ar) * t) >> 8);
            int g = ag + (((bg - ag) * t) >> 8);
            int b = ab + (((bb - ab) * t) >> 8);
            u8 pat[4];
            for (int p = 0; p < 4; p++)
                pat[p] = map_rgb_dith(r, g, b, p, oy + j);

            int i = 0;
            while (i < ow && ((u32)(row + i) & 3u)) { row[i] = pat[(ox + i) & 3]; i++; }
            int nw = (ow - i) >> 2;
            if (nw > 0) {
                u32 w32 = 0;
                for (int p = 0; p < 4; p++)
                    w32 |= (u32)pat[(ox + i + p) & 3] << (p * 8);
                u32 *wp = (u32 *)(row + i);
                for (int k = 0; k < nw; k++) wp[k] = w32;
                i += nw << 2;
            }
            for (; i < ow; i++) row[i] = pat[(ox + i) & 3];
        }
    } else if (dither_on) {
        for (int j = 0; j < oh; j++) {
            u8 *row = px + (oy + j) * pitch + ox;
            for (int i = 0; i < ow; i++) {
                int t = ((dx + i) << 8) / spanh;
                if (t > 256) t = 256;
                row[i] = map_rgb_dith(ar + (((br - ar) * t) >> 8),
                                      ag + (((bg - ag) * t) >> 8),
                                      ab + (((bb - ab) * t) >> 8), ox + i, oy + j);
            }
        }
    } else if (vertical) {
        for (int j = 0; j < oh; j++) {
            int t = ((dy + j) << 8) / spanv;
            if (t > 256) t = 256;
            u8 idx = map_rgb(ar + (((br - ar) * t) >> 8),
                             ag + (((bg - ag) * t) >> 8),
                             ab + (((bb - ab) * t) >> 8));
            memset(px + (oy + j) * pitch + ox, idx, ow);
        }
    } else {
        u8 line[2048];
        if (ow > 2048) ow = 2048;
        for (int i = 0; i < ow; i++) {
            int t = ((dx + i) << 8) / spanh;
            if (t > 256) t = 256;
            line[i] = map_rgb(ar + (((br - ar) * t) >> 8),
                              ag + (((bg - ag) * t) >> 8),
                              ab + (((bb - ab) * t) >> 8));
        }
        for (int j = 0; j < oh; j++)
            memcpy(px + (oy + j) * pitch + ox, line, ow);
    }
    api->surface_unlock();
    return ow * oh;
}

static int g_blit(int x, int y, int w, int h, const u8 *src, int spitch)
{
    u8 *px; int pitch, ox, oy, ow, oh, dx, dy;
    if (!clipr(x, y, w, h, &px, &pitch, &ox, &oy, &ow, &oh, &dx, &dy)) return 0;
    for (int j = 0; j < oh; j++)
        memcpy(px + (oy + j) * pitch + ox, src + (dy + j) * spitch + dx, ow);
    api->surface_unlock();
    return ow * oh;
}

static int g_rop2(int x, int y, int w, int h, u8 idx, int op)
{
    if (op == GR2_COPY) return g_fill(x, y, w, h, idx);
    u8 *px; int pitch, ox, oy, ow, oh, dx, dy;
    if (!clipr(x, y, w, h, &px, &pitch, &ox, &oy, &ow, &oh, &dx, &dy)) return 0;
    for (int j = 0; j < oh; j++) {
        u8 *row = px + (oy + j) * pitch + ox;
        for (int i = 0; i < ow; i++) {
            switch (op) {
            case GR2_XOR: row[i] ^= idx; break;
            case GR2_AND: row[i] &= idx; break;
            case GR2_OR:  row[i] |= idx; break;
            case GR2_NOT: row[i] = (u8)~row[i]; break;
            }
        }
    }
    api->surface_unlock();
    return ow * oh;
}

struct GRgn { int n; GRect r[RGN_MAXR]; };
static int rgn_live;

static GRgn *g_rgn_rect(int l, int t, int r, int b)
{
    GRgn *rg = (GRgn *)api->kmalloc(sizeof *rg);
    if (!rg) return 0;
    rgn_live++;
    if (r > l && b > t) { rg->r[0] = (GRect){ l, t, r, b }; rg->n = 1; }
    else rg->n = 0;
    return rg;
}
static void g_rgn_free(GRgn *r) { if (r) { api->kfree(r); rgn_live--; } }

static int g_rgn_combine(GRgn *dst, const GRgn *a, const GRgn *b, int mode)
{
    if (!dst || !a || !b) return -1;
    GRect tmp[RGN_MAXR];
    int n = rgn_op_raw(tmp, RGN_MAXR, a->r, a->n, b->r, b->n, mode);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) dst->r[i] = tmp[i];
    dst->n = n;
    return n;
}
static int g_rgn_offset(GRgn *r, int dx, int dy)
{
    if (!r) return -1;
    for (int i = 0; i < r->n; i++) {
        r->r[i].l += dx; r->r[i].r += dx;
        r->r[i].t += dy; r->r[i].b += dy;
    }
    return r->n;
}
static int g_rgn_pt_in(const GRgn *r, int x, int y)
{ return r ? rgn_pt_in_raw(r->r, r->n, x, y) : 0; }
static int g_rgn_box(const GRgn *r, GRect *bb)
{ return r ? rgn_box_raw(r->r, r->n, bb) : GRGN_NULL; }
static int g_rgn_bands(const GRgn *r, GRect *out, int max)
{
    if (!r || max <= 0) return 0;
    int n = r->n < max ? r->n : max;
    for (int i = 0; i < n; i++) out[i] = r->r[i];
    return n;
}
static int g_rgn_equal(const GRgn *a, const GRgn *b)
{ return (a && b) ? rgn_equal_raw(a->r, a->n, b->r, b->n) : 0; }

static int g_fill_rgn(const GRgn *r, GRGB c)
{
    if (!r) return 0;
    int total = 0;
    for (int i = 0; i < r->n; i++)
        total += g_fill_rgb(r->r[i].l, r->r[i].t,
                            r->r[i].r - r->r[i].l, r->r[i].b - r->r[i].t, c);
    return total;
}

static GRgn *g_rgn_polygon(const GPt *pts, int n, int mode)
{
    GRgn *rg = (GRgn *)api->kmalloc(sizeof *rg);
    if (!rg) return 0;
    rgn_live++;
    int nr = poly_to_bands(pts, n, mode, rg->r, RGN_MAXR);
    rg->n = nr > 0 ? nr : 0;
    return rg;
}

static int g_fill_poly(const GPt *pts, int n, GRGB c)
{
    if (n < 3) return 0;
    int ymin = pts[0].y, ymax = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < ymin) ymin = pts[i].y;
        if (pts[i].y > ymax) ymax = pts[i].y;
    }
    u8 idx = pal_index(c);
    u8 *sp; int pitch, pw, ph;
    if (!api->surface_lock(&sp, &pitch, &pw, &ph)) return 0;
    int cx, cy, cw2, ch2;
    api->clip_rect_get(&cx, &cy, &cw2, &ch2);
    int lx = cx < 0 ? 0 : cx, ly = cy < 0 ? 0 : cy;
    int hx = cx + cw2 > pw ? pw : cx + cw2, hy = cy + ch2 > ph ? ph : cy + ch2;
    if (ymin < ly) ymin = ly;
    if (ymax > hy) ymax = hy;
    i32 spans[2 * POLY_MAXV];
    int total = 0;
    for (int y = ymin; y < ymax; y++) {
        int ns = poly_scan(pts, n, y, GPF_EVENODD, spans, 2 * POLY_MAXV);
        for (int s = 0; s < ns; s++) {
            int x0 = spans[2 * s], x1 = spans[2 * s + 1];
            if (x0 < lx) x0 = lx;
            if (x1 > hx) x1 = hx;
            if (x1 > x0) { memset(sp + y * pitch + x0, idx, (u32)(x1 - x0)); total += x1 - x0; }
        }
    }
    api->surface_unlock();
    return total;
}

static int g_fill_beziers(const GPt *ctrl, int nseg, GRGB c)
{
    if (nseg < 1) return 0;
    GPt poly[POLY_MAXV];
    int np = 0;
    for (int s = 0; s < nseg; s++) {
        GPt seg[CURVE_MAXSEG + 1];
        int m = flatten_cubic(ctrl[3*s], ctrl[3*s+1], ctrl[3*s+2], ctrl[3*s+3],
                              seg, CURVE_MAXSEG + 1, 12);
        if (m < 0) continue;
        for (int i = np ? 1 : 0; i < m && np < POLY_MAXV; i++) poly[np++] = seg[i];
    }
    return g_fill_poly(poly, np, c);
}

static const signed short disc12[12][2] = {
    { 256,0 }, { 222,128 }, { 128,222 }, { 0,256 }, { -128,222 }, { -222,128 },
    { -256,0 }, { -222,-128 }, { -128,-222 }, { 0,-256 }, { 128,-222 }, { 222,-128 }
};
static int g_fill_disc(int cx, int cy, int r, GRGB c)
{
    GPt p[12];
    for (int i = 0; i < 12; i++) {
        p[i].x = cx + disc12[i][0] * r / 256;
        p[i].y = cy + disc12[i][1] * r / 256;
    }
    return g_fill_poly(p, 12, c);
}

static int g_stroke_poly(const GPt *pts, int n, int closed, int width, GRGB c)
{
    if (n < 2 || width < 1) return 0;
    int hw = width / 2; if (hw < 1) hw = 1;
    int total = 0;
    int nseg = closed ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        GPt q[4];
        if (stroke_seg(pts[i], pts[(i + 1) % n], hw, q)) total += g_fill_poly(q, 4, c);
    }
    for (int i = 0; i < n; i++) total += g_fill_disc(pts[i].x, pts[i].y, hw, c);
    return total;
}

struct lctx { u8 *px; int pitch, lx, ly, hx, hy, n; u8 idx; };
static void l_plot(void *vc, int x, int y)
{
    struct lctx *c = (struct lctx *)vc;
    if (x >= c->lx && x < c->hx && y >= c->ly && y < c->hy) {
        c->px[y * c->pitch + x] = c->idx;
        c->n++;
    }
}

static int g_pal_ramps(void)
{
    ensure_lut();
    return 1;
}

static int g_draw_line(int x0, int y0, int x1, int y1, GRGB c)
{
    struct lctx lc;
    int pw, ph, cx, cy, cw, ch;
    if (!api->surface_lock(&lc.px, &lc.pitch, &pw, &ph)) return 0;
    api->clip_rect_get(&cx, &cy, &cw, &ch);
    lc.lx = cx < 0 ? 0 : cx;             lc.ly = cy < 0 ? 0 : cy;
    lc.hx = cx + cw > pw ? pw : cx + cw; lc.hy = cy + ch > ph ? ph : cy + ch;
    lc.idx = pal_index(c);
    lc.n = 0;
    bres_line((GPt){ x0, y0 }, (GPt){ x1, y1 }, l_plot, &lc);
    api->surface_unlock();
    return lc.n;
}

#define AA_S 4
static int g_fill_poly_aa(const GPt *pts, int n, GRGB c)
{
    if (n < 3) return 0;
    int xmin = pts[0].x, xmax = pts[0].x, ymin = pts[0].y, ymax = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].x < xmin) xmin = pts[i].x; if (pts[i].x > xmax) xmax = pts[i].x;
        if (pts[i].y < ymin) ymin = pts[i].y; if (pts[i].y > ymax) ymax = pts[i].y;
    }
    u8 *sp; int pitch, pw, ph;
    if (!api->surface_lock(&sp, &pitch, &pw, &ph)) return 0;
    int clx, cly, clw, clh;
    api->clip_rect_get(&clx, &cly, &clw, &clh);
    int lx = clx < 0 ? 0 : clx, ly = cly < 0 ? 0 : cly;
    int hx = clx + clw > pw ? pw : clx + clw, hy = cly + clh > ph ? ph : cly + clh;
    if (xmin < lx) xmin = lx; if (xmax > hx) xmax = hx;
    if (ymin < ly) ymin = ly; if (ymax > hy) ymax = hy;
    if (xmin >= xmax || ymin >= ymax) { api->surface_unlock(); return 0; }
    ensure_lut();
    int cr = (c >> 16) & 0xFF, cg = (c >> 8) & 0xFF, cb = c & 0xFF;
    u8 full = map_rgb(cr, cg, cb);
    int w = xmax - xmin; if (w > 2048) w = 2048;
    u8 acc[2048];
    int xs[2 * POLY_MAXV], total = 0;
    for (int y = ymin; y < ymax; y++) {
        for (int i = 0; i < w; i++) acc[i] = 0;
        for (int sub = 0; sub < AA_S; sub++) {
            int ns = poly_scan_aa(pts, n, y * AA_S + sub, AA_S, GPF_EVENODD, xs, 2 * POLY_MAXV);
            for (int s = 0; s < ns; s++)
                span_cov(xs[2 * s], xs[2 * s + 1], AA_S, xmin, w, acc);
        }
        u8 *row = sp + y * pitch;
        for (int i = 0; i < w; i++) {
            int cov = acc[i];
            if (!cov) continue;
            int xp = xmin + i;
            if (cov >= AA_S * AA_S) { row[xp] = full; total++; continue; }
            u32 bl = rgb_lerp(rgb_of[row[xp]], c, cov * 16);
            row[xp] = map_rgb((bl >> 16) & 0xFF, (bl >> 8) & 0xFF, bl & 0xFF);
            total++;
        }
    }
    api->surface_unlock();
    return total;
}

static int g_caps(int what)
{
    u8 *px; int pitch, w, h;
    api->surface_lock(&px, &pitch, &w, &h);
    api->surface_unlock();
    switch (what) {
    case GCAP_BPP:          return 8;
    case GCAP_WIDTH:        return w;
    case GCAP_HEIGHT:       return h;
    case GCAP_PRESENT_MODE: return GP_DIRECT;
    case GCAP_HEAP_FREE:    return (int)api->heap_avail();
    case GCAP_PALETTE_FREE: return 208;
    case GCAP_AA:           ensure_lut(); return lut ? 1 : 0;
    case GCAP_LUT_US:       return (int)lut_us;
    case GCAP_DITHER:       return dither_on;
    case GCAP_OBJECTS:      return rgn_live;
    case GCAP_FPU: {
        u32 cr0;
        __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
        return (cr0 & (1u << 2)) ? 0 : 1;
    }
    }
    return -1;
}

static const GdiOps gdi_ops = {
    .abi = GDI_ABI,
    .fill = g_fill, .fill_rgb = g_fill_rgb, .blit = g_blit,
    .rop2 = g_rop2, .caps = g_caps,
    .pal_build = pal_build, .pal_index = pal_index, .pal_unpack = pal_unpack,
    .fill_gradient = g_fill_gradient,
    .set_dither = g_set_dither,
    .rgn_rect = g_rgn_rect, .rgn_free = g_rgn_free, .rgn_combine = g_rgn_combine,
    .rgn_offset = g_rgn_offset, .rgn_pt_in = g_rgn_pt_in, .rgn_box = g_rgn_box,
    .rgn_bands = g_rgn_bands, .rgn_equal = g_rgn_equal, .fill_rgn = g_fill_rgn,
    .fill_poly = g_fill_poly,
    .rgn_polygon = g_rgn_polygon,
    .fill_beziers = g_fill_beziers,
    .stroke_poly = g_stroke_poly,
    .fill_poly_aa = g_fill_poly_aa,
    .draw_line = g_draw_line,
    .pal_ramps = g_pal_ramps,
};

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "gdi"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    if (api->register_service("gdi", &gdi_ops) < 0) return 1;

    ensure_lut();
    return 0;
}
