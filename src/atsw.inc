/* Chooses the next window for Alt+Tab. */
#pragma once

static inline int at_next(int nz, int k, int dir)
{
    if (nz <= 0) return -1;
    k += dir > 0 ? -1 : 1;
    if (k < 0) k = nz - 1;
    if (k >= nz) k = 0;
    return k;
}
