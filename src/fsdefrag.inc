/* Moves file data during floppy defragmentation. */
enum {
    FSD_MOVED    = 0,
    FSD_INTACT   = -1,
    FSD_RESTORED = -2,
    FSD_TORN     = -3
};

static inline int fsd_move(int (*rd)(u32 lba, u8 *sec),
                           int (*wr)(u32 lba, const u8 *sec),
                           u32 S, u32 D, u32 L, u8 *sec)
{
    if (D == S || !L) return FSD_MOVED;
    u32 g = S - D;

    if (L > g)
        for (u32 s = 0; s < L; s++)
            if (rd(S + s, sec) != 0)
                return FSD_INTACT;

    u32 F = L;
    int wfail = 0;
    for (u32 s = 0; s < L; s++) {
        if (rd(S + s, sec) != 0) { F = s; break; }
        if (wr(D + s, sec) != 0) { F = s; wfail = 1; break; }
    }
    if (F == L) return FSD_MOVED;

    u32 nback = (L <= g) ? 0 : (F < g ? 0 : (wfail ? F - g + 1 : F - g));
    if (!nback) return FSD_INTACT;

    for (u32 k = nback; k-- > 0; )
        if (rd(D + k, sec) != 0 || wr(S + k, sec) != 0)
            return FSD_TORN;
    return FSD_RESTORED;
}
