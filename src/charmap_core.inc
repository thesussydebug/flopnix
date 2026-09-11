/* Maps character cells and selections. */
#pragma once

static int cm_hit(int x, int y, int gx, int gy, int cw, int ch)
{
    if (cw <= 0 || ch <= 0 || x < gx || y < gy || x >= gx + cw*16 || y >= gy + ch*16) return -1;
    return (y-gy)/ch*16 + (x-gx)/cw;
}
