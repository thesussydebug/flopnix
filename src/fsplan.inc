/* Plans file positions for floppy defragmentation. */
#include "fsbad.inc"

typedef struct {
    u16 start, sects;
    u16 nstart;
    u8  used;
    u16 idx;
} FpEnt;

static inline int fs_plan(FpEnt *e, int n, u16 data0, const u16 *bad, int nbad)
{

    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!e[i].used) continue;
        FpEnt t = e[i];
        int j = m - 1;
        while (j >= 0 && e[j].start > t.start) { e[j + 1] = e[j]; j--; }
        e[j + 1] = t;
        m++;
    }
    int moves = 0;
    u32 next = data0;
    for (int i = 0; i < m; i++) {
        u32 at = next;
        int b;
        while (at < e[i].start && (b = fb_clash(bad, nbad, at, e[i].sects)) >= 0)
            at = (u32)b + 1;
        if (at > e[i].start || fb_clash(bad, nbad, e[i].start, e[i].sects) >= 0)
            at = e[i].start;
        e[i].nstart = (u16)at;
        next = at + e[i].sects;
        if (e[i].nstart != e[i].start) moves++;
    }

    for (int i = m; i < n; i++) e[i].used = 0;
    return moves;
}
