/* Plans file positions for floppy defragmentation. */
typedef struct {
    u16 start, sects;
    u16 nstart;
    u8  used;
    u8  idx;
} FpEnt;

static inline int fs_plan(FpEnt *e, int n, u16 data0)
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
    u16 next = data0;
    for (int i = 0; i < m; i++) {
        e[i].nstart = next;
        next = (u16)(next + e[i].sects);
        if (e[i].nstart != e[i].start) moves++;
    }

    for (int i = m; i < n; i++) e[i].used = 0;
    return moves;
}
