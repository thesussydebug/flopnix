/* Assembles PS/2 mouse packets. */
#pragma once

#define MS2_GAP 2

typedef struct {
    u8  pk[4];
    u8  idx;
    u32 last;
} Ms2Asm;

static inline int ms2_feed(Ms2Asm *a, u8 b, u32 now, int plen, u32 *out)
{
    if (a->idx && (u32)(now - a->last) >= MS2_GAP)
        a->idx = 0;
    a->last = now;
    if (a->idx == 0 && !(b & 0x08)) return 0;
    a->pk[a->idx++] = b;
    if (a->idx < plen) return 0;
    a->idx = 0;
    *out = a->pk[0] | ((u32)a->pk[1] << 8) | ((u32)a->pk[2] << 16) |
           (plen == 4 ? (u32)a->pk[3] << 24 : 0);
    return 1;
}
