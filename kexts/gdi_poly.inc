#pragma once

#define POLY_MAXV 256

static int poly_ceil_div(int a, int b) { return a >= 0 ? (a + b - 1) / b : a / b; }

static int poly_scan(const GPt *p, int n, int y, int mode, i32 *spans, int cap)
{
    if (n < 3) return 0;
    int px[POLY_MAXV], pd[POLY_MAXV], pdir[POLY_MAXV];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const GPt *a = &p[i], *b = &p[(i + 1) % n];
        int ylo = a->y < b->y ? a->y : b->y;
        int yhi = a->y < b->y ? b->y : a->y;
        if (y < ylo || y >= yhi) continue;

        int den = 2 * (b->y - a->y);
        int num = (b->x - a->x) * (2 * y + 1 - 2 * a->y);
        int P = a->x * den + num;
        if (den < 0) { den = -den; P = -P; }
        if (m < POLY_MAXV) { px[m] = P; pd[m] = den; pdir[m] = b->y > a->y ? 1 : -1; m++; }
    }

    for (int i = 1; i < m; i++) {
        int vp = px[i], vd = pd[i], vs = pdir[i], j = i - 1;
        while (j >= 0 && px[j] * vd > vp * pd[j]) {
            px[j + 1] = px[j]; pd[j + 1] = pd[j]; pdir[j + 1] = pdir[j]; j--;
        }
        px[j + 1] = vp; pd[j + 1] = vd; pdir[j + 1] = vs;
    }

    int ns = 0, wind = 0;
    for (int i = 0; i + 1 < m; i++) {
        wind += pdir[i];
        int inside = mode == GPF_WINDING ? wind != 0 : !(i & 1);
        if (!inside) continue;
        int L = poly_ceil_div(2 * px[i]     - pd[i],     2 * pd[i]);
        int R = poly_ceil_div(2 * px[i + 1] - pd[i + 1], 2 * pd[i + 1]);
        if (R <= L) continue;
        if (ns > 0 && spans[2 * ns - 1] == L) spans[2 * ns - 1] = R;
        else { if (2 * ns + 1 >= cap) return -1; spans[2 * ns] = L; spans[2 * ns + 1] = R; ns++; }
    }
    return ns;
}

static int poly_to_bands(const GPt *p, int n, int mode, GRect *out, int cap)
{
    if (n < 3) return 0;
    int ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    i32 cur[2 * POLY_MAXV], prev[2 * POLY_MAXV];
    int prevns = 0, prev_t = 0, have = 0, nrect = 0;
    for (int y = ymin; y <= ymax; y++) {
        int ns = y < ymax ? poly_scan(p, n, y, mode, cur, 2 * POLY_MAXV) : 0;
        if (ns < 0) return -1;
        if (have && rgn_ivs_equal(prev, prevns, cur, ns)) continue;
        if (have) {
            for (int s = 0; s < prevns; s++) {
                if (nrect >= cap) return -1;
                out[nrect].l = prev[2 * s]; out[nrect].t = prev_t;
                out[nrect].r = prev[2 * s + 1]; out[nrect].b = y; nrect++;
            }
            have = 0;
        }
        if (ns > 0) {
            for (int s = 0; s < 2 * ns; s++) prev[s] = cur[s];
            prevns = ns; prev_t = y; have = 1;
        }
    }
    return nrect;
}

static int poly_scan_aa(const GPt *p, int n, int subY, int aas, int mode,
                        int *xs, int cap)
{
    if (n < 3) return 0;
    int cx[POLY_MAXV], dir[POLY_MAXV], m = 0;
    for (int i = 0; i < n; i++) {
        const GPt *a = &p[i], *b = &p[(i + 1) % n];
        int ylo = a->y < b->y ? a->y : b->y;
        int yhi = a->y < b->y ? b->y : a->y;
        if (subY < ylo * aas || subY >= yhi * aas) continue;

        int dy = b->y - a->y, dx = b->x - a->x, den = 2 * dy;
        int P = aas * a->x * den + dx * (2 * subY + 1 - 2 * aas * a->y);
        if (den < 0) { den = -den; P = -P; }
        int x = P >= 0 ? (P + den / 2) / den : -(((-P) + den / 2) / den);
        if (m < POLY_MAXV) { cx[m] = x; dir[m] = b->y > a->y ? 1 : -1; m++; }
    }
    for (int i = 1; i < m; i++) {
        int vx = cx[i], vd = dir[i], j = i - 1;
        while (j >= 0 && cx[j] > vx) { cx[j + 1] = cx[j]; dir[j + 1] = dir[j]; j--; }
        cx[j + 1] = vx; dir[j + 1] = vd;
    }
    int ns = 0, wind = 0;
    for (int i = 0; i + 1 < m; i++) {
        wind += dir[i];
        int inside = mode == GPF_WINDING ? wind != 0 : !(i & 1);
        if (!inside) continue;
        int L = cx[i], R = cx[i + 1];
        if (R <= L) continue;
        if (ns > 0 && xs[2 * ns - 1] == L) xs[2 * ns - 1] = R;
        else { if (2 * ns + 1 >= cap) return -1; xs[2 * ns] = L; xs[2 * ns + 1] = R; ns++; }
    }
    return ns;
}
