#pragma once

#define RGN_MAXR   256
#define RGN_IV1    (2 * RGN_MAXR + 4)
#define RGN_IV2    (4 * RGN_MAXR + 4)
#define RGN_MAXY   (2 * RGN_MAXR + 2)

static int rgn_iv_op(i32 *out, const i32 *a, int na, const i32 *b, int nb, int op)
{
    int ia = 0, ib = 0, no = 0, inA = 0, inB = 0, open = 0;
    i32 start = 0;
    while (ia < na || ib < nb) {
        i32 ax = (ia < na) ? (inA ? a[2 * ia + 1] : a[2 * ia]) : (i32)0x7FFFFFFF;
        i32 bx = (ib < nb) ? (inB ? b[2 * ib + 1] : b[2 * ib]) : (i32)0x7FFFFFFF;
        i32 x = ax < bx ? ax : bx;
        if (ax == x) { if (inA) { inA = 0; ia++; } else inA = 1; }
        if (bx == x) { if (inB) { inB = 0; ib++; } else inB = 1; }
        int want;
        switch (op) {
            case RGN_AND:  want = inA && inB;  break;
            case RGN_XOR:  want = inA ^ inB;   break;
            case RGN_DIFF: want = inA && !inB; break;
            default:       want = inA || inB;  break;
        }
        if (want && !open) { start = x; open = 1; }
        else if (!want && open) { out[2 * no] = start; out[2 * no + 1] = x; no++; open = 0; }
    }
    return no;
}

static int rgn_band_ivs(i32 *iv, const GRect *r, int n, i32 y0)
{
    int k = 0;
    for (int i = 0; i < n; i++)
        if (r[i].t <= y0 && r[i].b > y0) { iv[2 * k] = r[i].l; iv[2 * k + 1] = r[i].r; k++; }
    return k;
}

static int rgn_ivs_equal(const i32 *a, int na, const i32 *b, int nb)
{
    if (na != nb) return 0;
    for (int i = 0; i < 2 * na; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int rgn_emit(GRect *out, int *n, int cap, const i32 *iv, int niv, i32 y0, i32 y1)
{
    for (int i = 0; i < niv; i++) {
        if (*n >= cap) return -1;
        out[*n].l = iv[2 * i]; out[*n].t = y0;
        out[*n].r = iv[2 * i + 1]; out[*n].b = y1;
        (*n)++;
    }
    return 0;
}

static int rgn_op_raw(GRect *out, int cap, const GRect *a, int an,
                      const GRect *b, int bn, int op)
{
    if (an < 0 || bn < 0 || an > RGN_MAXR || bn > RGN_MAXR) return -1;
    if (op == RGN_COPY) {
        if (an > cap) return -1;
        for (int i = 0; i < an; i++) out[i] = a[i];
        return an;
    }

    i32 ys[RGN_MAXY]; int ny = 0;
    for (int i = 0; i < an; i++) { ys[ny++] = a[i].t; ys[ny++] = a[i].b; }
    for (int i = 0; i < bn; i++) { ys[ny++] = b[i].t; ys[ny++] = b[i].b; }
    for (int i = 1; i < ny; i++) {
        i32 v = ys[i]; int j = i - 1;
        while (j >= 0 && ys[j] > v) { ys[j + 1] = ys[j]; j--; }
        ys[j + 1] = v;
    }
    int uy = 0;
    for (int i = 0; i < ny; i++) if (uy == 0 || ys[i] != ys[uy - 1]) ys[uy++] = ys[i];
    ny = uy;

    i32 aiv[RGN_IV1], biv[RGN_IV1], riv[RGN_IV2], piv[RGN_IV2];
    int pn = 0, n = 0, have = 0;
    i32 py0 = 0, py1 = 0;
    for (int k = 0; k + 1 < ny; k++) {
        i32 y0 = ys[k], y1 = ys[k + 1];
        int na = rgn_band_ivs(aiv, a, an, y0);
        int nb = rgn_band_ivs(biv, b, bn, y0);
        int nr = rgn_iv_op(riv, aiv, na, biv, nb, op);

        if (have && py1 == y0 && rgn_ivs_equal(piv, pn, riv, nr)) { py1 = y1; continue; }
        if (have) { if (rgn_emit(out, &n, cap, piv, pn, py0, py1) < 0) return -1; have = 0; }
        if (nr > 0) {
            for (int i = 0; i < 2 * nr; i++) piv[i] = riv[i];
            pn = nr; py0 = y0; py1 = y1; have = 1;
        }
    }
    if (have) { if (rgn_emit(out, &n, cap, piv, pn, py0, py1) < 0) return -1; }
    return n;
}

static int rgn_pt_in_raw(const GRect *r, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        if (x >= r[i].l && x < r[i].r && y >= r[i].t && y < r[i].b) return 1;
    return 0;
}

static int rgn_box_raw(const GRect *r, int n, GRect *bbox)
{
    if (n <= 0) { if (bbox) { bbox->l = bbox->t = bbox->r = bbox->b = 0; } return GRGN_NULL; }
    i32 l = r[0].l, t = r[0].t, rr = r[0].r, b = r[0].b;
    for (int i = 1; i < n; i++) {
        if (r[i].l < l) l = r[i].l;   if (r[i].t < t) t = r[i].t;
        if (r[i].r > rr) rr = r[i].r; if (r[i].b > b) b = r[i].b;
    }
    if (bbox) { bbox->l = l; bbox->t = t; bbox->r = rr; bbox->b = b; }
    return n == 1 ? GRGN_SIMPLE : GRGN_COMPLEX;
}

static int rgn_equal_raw(const GRect *a, int an, const GRect *b, int bn)
{
    if (an != bn) return 0;
    for (int i = 0; i < an; i++)
        if (a[i].l != b[i].l || a[i].t != b[i].t || a[i].r != b[i].r || a[i].b != b[i].b)
            return 0;
    return 1;
}
