/* Tracks active device operations and rejects nested access. */
typedef u8 IoGuard;

static inline int io_acquire(IoGuard *g)
{
    u32 fl;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(fl) :: "memory");
    int got = !*g;
    if (got) *g = 1;
    if (fl & 0x200) __asm__ volatile("sti" ::: "memory");
    return got;
}

static inline void io_release(IoGuard *g)
{
    *g = 0;
}

static inline int io_held(const IoGuard *g)
{
    return *g != 0;
}
