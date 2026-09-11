/* Calculates a scaled product with overflow limits. */
static inline u32 bm_muldiv(u32 a, u32 b, u32 d)
{
    if (!d) return 0;
    u32 hi, lo;
    __asm__("mull %3" : "=a"(lo), "=d"(hi) : "a"(a), "rm"(b) : "cc");
    if (hi >= d) return 0xffffffffu;
    u32 rem;
    __asm__("divl %4" : "=a"(lo), "=d"(rem) : "a"(lo), "d"(hi), "rm"(d) : "cc");
    return lo;
}
static inline u32 bm_median(u32 a, u32 b, u32 c)
{
    if (a > b) { u32 t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return a > b ? a : b;
}

static inline int bm_stalled(u32 now, u32 *last, u32 *spins, u32 limit)
{
    if (now != *last) { *last = now; *spins = 0; return 0; }
    return ++*spins >= limit;
}
