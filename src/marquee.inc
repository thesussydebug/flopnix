/* Checks which icons overlap a selection box. */
#pragma once

static inline void mq_norm(int a, int b, int *lo, int *hi)
{
    if (a <= b) { *lo = a; *hi = b; }
    else        { *lo = b; *hi = a; }
}

static inline int mq_hit(int bx0, int by0, int bx1, int by1,
                         int ix, int iy, int iw, int ih)
{
    int lx, hx, ly, hy;
    mq_norm(bx0, bx1, &lx, &hx);
    mq_norm(by0, by1, &ly, &hy);

    return lx < ix + iw && hx >= ix && ly < iy + ih && hy >= iy;
}
