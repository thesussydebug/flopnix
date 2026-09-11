#pragma once

static const u32 g3_qsin[257] = {
    0, 402, 804, 1206, 1608, 2010, 2412, 2814,
    3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
    6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218,
    9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391,
    12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534,
    15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639,
    19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699,
    22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708,
    25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656,
    28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
    30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347,
    33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075,
    36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716,
    39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264,
    41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713,
    44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056,
    46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288,
    48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404,
    50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398,
    52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
    54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004,
    56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607,
    57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071,
    59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392,
    60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568,
    61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596,
    62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473,
    63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197,
    64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766,
    64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
    65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436,
    65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535,
    65536,
};

static fx fx_sin(int brads)
{
    u32 a = (u32)brads & 0xFFFFu;
    int q = (int)(a >> 14), r = (int)(a & 0x3FFF);
    if (q & 1) r = 0x4000 - r;
    fx v;
    if (r >= 0x4000) v = FX_ONE;
    else {
        int i = r >> 6, f = r & 63;
        v = (fx)(g3_qsin[i] + (((g3_qsin[i + 1] - g3_qsin[i]) * (u32)f) >> 6));
    }
    return q >= 2 ? -v : v;
}
static fx fx_cos(int brads) { return fx_sin(brads + 16384); }

static void m4_ident(fx *m)
{
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = FX_ONE;
}

static void m4_mul(fx *o, const fx *a, const fx *b)
{
    fx t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            fx s = 0;
            for (int k = 0; k < 4; k++)
                s += fx_mul(a[k * 4 + r], b[c * 4 + k]);
            t[c * 4 + r] = s;
        }
    for (int i = 0; i < 16; i++) o[i] = t[i];
}

static void m4_vec(GVec *o, const fx *m, const GVec *v)
{
    fx x = v->x, y = v->y, z = v->z, w = v->w;
    o->x = fx_mul(m[0], x) + fx_mul(m[4], y) + fx_mul(m[8],  z) + fx_mul(m[12], w);
    o->y = fx_mul(m[1], x) + fx_mul(m[5], y) + fx_mul(m[9],  z) + fx_mul(m[13], w);
    o->z = fx_mul(m[2], x) + fx_mul(m[6], y) + fx_mul(m[10], z) + fx_mul(m[14], w);
    o->w = fx_mul(m[3], x) + fx_mul(m[7], y) + fx_mul(m[11], z) + fx_mul(m[15], w);
}

static void m4_trans(fx *m, fx x, fx y, fx z)
{
    m4_ident(m);
    m[12] = x; m[13] = y; m[14] = z;
}

static void m4_rot_x(fx *m, int brads)
{
    fx c = fx_cos(brads), s = fx_sin(brads);
    m4_ident(m);
    m[5] = c; m[6] = s; m[9] = -s; m[10] = c;
}
static void m4_rot_y(fx *m, int brads)
{
    fx c = fx_cos(brads), s = fx_sin(brads);
    m4_ident(m);
    m[0] = c; m[2] = -s; m[8] = s; m[10] = c;
}
static void m4_rot_z(fx *m, int brads)
{
    fx c = fx_cos(brads), s = fx_sin(brads);
    m4_ident(m);
    m[0] = c; m[1] = s; m[4] = -s; m[5] = c;
}

static int g3_deg2brad(fx deg) { return fx_mul(deg, 11930465) >> 16; }

static void m4_persp(fx *m, fx fovy_deg, fx aspect, fx zn, fx zf)
{
    int hb = g3_deg2brad(fovy_deg) / 2;
    fx f = fx_div(fx_cos(hb), fx_sin(hb));
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0]  = fx_div(f, aspect);
    m[5]  = f;
    m[10] = fx_div(zn + zf, zn - zf);
    m[11] = -FX_ONE;
    m[14] = 2 * fx_div(fx_mul(zf, zn), zn - zf);
}

#define G3_WEPS 4096
static void g3_project_vp(GVec *o, const GVec *c, int vx, int vy, int vw, int vh)
{
    fx nx = fx_clamp(fx_div(c->x, c->w), -FX(8), FX(8));
    fx ny = fx_clamp(fx_div(c->y, c->w), -FX(8), FX(8));
    o->x = FX(vx) + fx_mul(nx + FX_ONE, FX(vw) / 2);
    o->y = FX(vy) + fx_mul(FX_ONE - ny, FX(vh) / 2);
    o->z = fx_div(c->z, c->w);
    o->w = c->w;
}

static fx g3_pdist(const GVec *v, int p)
{
    fx gw = fx_mul(FX(4), v->w);
    switch (p) {
    case 0:  return v->w - G3_WEPS;
    case 1:  return gw - v->x;
    case 2:  return v->x + gw;
    case 3:  return gw - v->y;
    default: return v->y + gw;
    }
}

static GVec g3_vlerp(const GVec *a, const GVec *b, fx t)
{
    GVec r;
    r.x = a->x + fx_mul(b->x - a->x, t);
    r.y = a->y + fx_mul(b->y - a->y, t);
    r.z = a->z + fx_mul(b->z - a->z, t);
    r.w = a->w + fx_mul(b->w - a->w, t);
    return r;
}

static int g3_clip_seg(GVec *a, GVec *b)
{
    fx t0 = 0, t1 = FX_ONE;
    for (int p = 0; p < 5; p++) {
        fx d0 = g3_pdist(a, p), d1 = g3_pdist(b, p);
        if (d0 < 0 && d1 < 0) return 0;
        if (d0 < 0) { fx t = fx_div(d0, d0 - d1); if (t > t0) t0 = t; }
        else if (d1 < 0) { fx t = fx_div(d0, d0 - d1); if (t < t1) t1 = t; }
    }
    if (t0 >= t1) return 0;
    GVec na = t0 > 0 ? g3_vlerp(a, b, t0) : *a;
    GVec nb = t1 < FX_ONE ? g3_vlerp(a, b, t1) : *b;
    *a = na; *b = nb;
    return 1;
}

static fx g3_dot3(const GVec *a, const GVec *b)
{
    return fx_mul(a->x, b->x) + fx_mul(a->y, b->y) + fx_mul(a->z, b->z);
}

static GVec g3_cross3(const GVec *a, const GVec *b)
{
    GVec r;
    r.x = fx_mul(a->y, b->z) - fx_mul(a->z, b->y);
    r.y = fx_mul(a->z, b->x) - fx_mul(a->x, b->z);
    r.z = fx_mul(a->x, b->y) - fx_mul(a->y, b->x);
    r.w = 0;
    return r;
}

static void g3_norm3(GVec *v)
{
    fx len = fx_sqrt(g3_dot3(v, v));
    if (len <= 0) return;
    v->x = fx_div(v->x, len);
    v->y = fx_div(v->y, len);
    v->z = fx_div(v->z, len);
}

static int g3_light_level(const GVec *n, const G3Light *L, int nl,
                          fx amb, fx mtl_a, fx mtl_d)
{
    fx sum = fx_mul(mtl_a, amb);
    for (int i = 0; i < nl; i++) {
        fx d = -g3_dot3(n, &L[i].dir);
        if (d > 0) sum += fx_mul(mtl_d, fx_mul(L[i].diffuse, d));
    }
    if (sum < 0) sum = 0;
    if (sum > FX_ONE) sum = FX_ONE;
    return (int)((sum * 63) >> 16);
}

static void m4_lookat(fx *m, const GVec *eye, const GVec *at, const GVec *up)
{
    GVec f = { at->x - eye->x, at->y - eye->y, at->z - eye->z, 0 };
    g3_norm3(&f);
    GVec s = g3_cross3(&f, up);
    g3_norm3(&s);
    GVec u = g3_cross3(&s, &f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = s.x; m[4] = s.y; m[8]  = s.z;
    m[1] = u.x; m[5] = u.y; m[9]  = u.z;
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z;
    m[12] = -g3_dot3(&s, eye);
    m[13] = -g3_dot3(&u, eye);
    m[14] = g3_dot3(&f, eye);
    m[15] = FX_ONE;
}

static u32 g3_shade_rgb(u32 rgb, int level)
{
    int m = (level < 0 ? 0 : level > 63 ? 63 : level) + 1;
    u32 r = (((rgb >> 16) & 0xFF) * (u32)m) >> 6;
    u32 g = (((rgb >> 8) & 0xFF) * (u32)m) >> 6;
    u32 b = ((rgb & 0xFF) * (u32)m) >> 6;
    return (r << 16) | (g << 8) | b;
}

static int g3_lum6(u32 rgb)
{
    u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u32 m = r > g ? r : g;
    if (b > m) m = b;
    return (int)(m >> 2);
}

static int g3_zmap16(fx zndc)
{
    if (zndc <= -FX_ONE) return 0;
    if (zndc >= FX_ONE - 1) return 65535;
    return (int)(((u32)(zndc + FX_ONE)) >> 1);
}

static void g3_prim_tri(int prim, int k, int *i0, int *i1, int *i2)
{
    switch (prim) {
    case G3P_TRISTRIP:
        if (k & 1) { *i0 = k + 1; *i1 = k; *i2 = k + 2; }
        else       { *i0 = k; *i1 = k + 1; *i2 = k + 2; }
        break;
    case G3P_TRIFAN:
        *i0 = 0; *i1 = k + 1; *i2 = k + 2;
        break;
    default:
        *i0 = 3 * k; *i1 = 3 * k + 1; *i2 = 3 * k + 2;
    }
}

typedef struct { fx x, y, z, l; } G3SV;

static G3SV g3_svlerp(const G3SV *a, const G3SV *b, fx t)
{
    G3SV r;
    r.x = a->x + fx_mul(b->x - a->x, t);
    r.y = a->y + fx_mul(b->y - a->y, t);
    r.z = a->z + fx_mul(b->z - a->z, t);
    r.l = a->l + fx_mul(b->l - a->l, t);
    return r;
}

static int tri_scan(const G3SV *v0, const G3SV *v1, const G3SV *v2,
                    int y, G3SV *L, G3SV *R)
{
    const G3SV *e[3][2] = { { v0, v1 }, { v1, v2 }, { v2, v0 } };
    fx yc = FX(y) + 32768;
    G3SV hit[3];
    int m = 0;
    for (int i = 0; i < 3; i++) {
        const G3SV *a = e[i][0], *b = e[i][1];
        fx ylo = a->y < b->y ? a->y : b->y;
        fx yhi = a->y < b->y ? b->y : a->y;
        if (yc < ylo || yc >= yhi) continue;
        if (m < 3) hit[m++] = g3_svlerp(a, b, fx_div(yc - a->y, b->y - a->y));
    }
    if (m < 2) return 0;
    int lo = hit[0].x <= hit[1].x ? 0 : 1;
    *L = hit[lo]; *R = hit[lo ^ 1];
    return 1;
}

static int g3_clip_polya(GVec *io, fx *a, int n)
{
    GVec vb[16]; fx ab[16];
    GVec *vs = io, *vd = vb;
    fx *as = a, *ad = ab;
    for (int p = 0; p < 5 && n; p++) {
        int m = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            fx dc = g3_pdist(&vs[i], p), dn = g3_pdist(&vs[j], p);
            if (dc >= 0) {
                if (m < 16) { vd[m] = vs[i]; ad[m] = as[i]; m++; }
                if (dn < 0 && m < 16) {
                    fx t = fx_div(dc, dc - dn);
                    vd[m] = g3_vlerp(&vs[i], &vs[j], t);
                    ad[m] = as[i] + fx_mul(as[j] - as[i], t);
                    m++;
                }
            } else if (dn >= 0) {
                fx t = fx_div(dc, dc - dn);
                if (m < 16) {
                    vd[m] = g3_vlerp(&vs[i], &vs[j], t);
                    ad[m] = as[i] + fx_mul(as[j] - as[i], t);
                    m++;
                }
            }
        }
        n = m;
        { GVec *tv = vs; vs = vd; vd = tv; }
        { fx *ta = as; as = ad; ad = ta; }
    }
    if (vs != io)
        for (int i = 0; i < n; i++) { io[i] = vs[i]; a[i] = as[i]; }
    return n;
}

static inline int g3_clip_poly(GVec *io, int n)
{
    fx scratch[16];
    for (int i = 0; i < n && i < 16; i++) scratch[i] = 0;
    return g3_clip_polya(io, scratch, n);
}
