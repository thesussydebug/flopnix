#pragma once

static inline int sv_bounce(u32 d, int range, u32 *hits)
{
    if (range <= 0) { *hits = 0; return 0; }
    u32 r = (u32)range, m = d % (2 * r);
    *hits = d / r;
    return (int)(m <= r ? m : 2 * r - m);
}
