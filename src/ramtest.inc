/* Builds and checks memory-test patterns. */
#define RT_PASSES 3

static inline u32 rt_pattern(u32 addr, int pass)
{
    if (pass == 0) return 0x55555555u;
    if (pass == 1) return 0xAAAAAAAAu;
    return addr ^ 0x464C4F50u;
}

static inline void rt_fill(u32 *p, u32 words, u32 base, int pass)
{
    for (u32 i = 0; i < words; i++)
        p[i] = rt_pattern(base + i * 4, pass);
}

static inline i32 rt_check(const u32 *p, u32 words, u32 base, int pass)
{
    for (u32 i = 0; i < words; i++)
        if (p[i] != rt_pattern(base + i * 4, pass)) return (i32)i;
    return -1;
}
