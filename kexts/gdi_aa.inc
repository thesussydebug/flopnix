#pragma once

static u32 rgb_lerp(u32 a, u32 b, int t)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (((br - ar) * t) >> 8);
    int g = ag + (((bg - ag) * t) >> 8);
    int bl = ab + (((bb - ab) * t) >> 8);
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

static void span_cov(int xL, int xR, int S, int x0, int w, u8 *acc)
{
    if (xR <= xL) return;
    int pL = xL / S, pR = (xR - 1) / S;
    if (pL < x0) pL = x0;
    if (pR > x0 + w - 1) pR = x0 + w - 1;
    for (int px = pL; px <= pR; px++) {
        int lo = px * S, hi = lo + S;
        int a = xL > lo ? xL : lo;
        int b = xR < hi ? xR : hi;
        if (b > a) acc[px - x0] += (u8)(b - a);
    }
}
