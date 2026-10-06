#pragma once

static inline int shot_clip(int sw, int sh, int *x, int *y, int *w, int *h)
{
    int x0 = *x < 0 ? 0 : *x, y0 = *y < 0 ? 0 : *y;
    int x1 = *x + *w, y1 = *y + *h;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x1 <= x0 || y1 <= y0) return 0;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return 1;
}
