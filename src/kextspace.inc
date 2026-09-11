/* Calculates each extension's private memory layout. */
#define KEXT_PRIV_VA   0x40000000u
#define KEXT_PRIV_SIZE 0x00400000u
#define KEXT_PRIV_PDE  (KEXT_PRIV_VA >> 22)

#ifdef KERNEL_MEMORY_LAYOUT
#define KEXT_POOL_BASE (memory.pool)
#define KEXT_POOL_END (memory.pool_end)
#define ARENA_BASE (memory.arena)
#define ARENA_END (memory.arena_end)
#else
#define KEXT_POOL_BASE 0x00800000u
#define KEXT_POOL_END  0x00900000u

#define ARENA_BASE 0x00600000u
#define ARENA_END  0x00700000u
#endif

#define SHF_WRITE_BIT 1

#define KEXT_PRIVATE_ENABLED 1

static inline int ks_is_private(u32 sh_flags, int kind)
{
    if (!KEXT_PRIVATE_ENABLED) return 0;
    if (kind != KEXT_KIND_APP) return 0;
    return (sh_flags & SHF_WRITE_BIT) != 0;
}

static inline u32 ks_place(u32 *cursor, u32 size, u32 align)
{
    if (align < 1) align = 1;
    u32 at = (*cursor + align - 1) & ~(align - 1);
    *cursor = at + size;
    return at;
}

static inline int ks_pool_fits(u32 used, u32 n)
{
    u32 cap = KEXT_POOL_END - KEXT_POOL_BASE;
    if (n > cap || used > cap) return 0;
    return n <= cap - used;
}

static inline int ks_pool_pages_fit(u32 used, u32 n) {
    if (n > KEXT_POOL_END - KEXT_POOL_BASE) return 0;
    return ks_pool_fits(used, (n + 0xFFFu) & ~0xFFFu);
}

#define KEXT_PD_MAX 28

static inline u32 ks_space_base(int slot)
{
    return KEXT_PRIV_VA + (u32)slot * KEXT_PRIV_SIZE;
}

static inline u32 ks_space_pde(int slot)
{
    return (KEXT_PRIV_VA >> 22) + (u32)slot;
}

static inline u32 ks_priv_va_slot(int slot, u32 off)
{
    return ks_space_base(slot) + off;
}

static inline int ks_space_of(u32 va)
{
    if (va < KEXT_PRIV_VA) return -1;
    u32 n = (va - KEXT_PRIV_VA) / KEXT_PRIV_SIZE;
    return n < KEXT_PD_MAX ? (int)n : -1;
}

static inline u32 ks_priv_va(u32 off) { return KEXT_PRIV_VA + off; }

static inline int ks_in_priv(u32 va)
{
    return va >= KEXT_PRIV_VA && va < KEXT_PRIV_VA + KEXT_PRIV_SIZE;
}

static inline int ks_owner(int loading, int current)
{
    return loading >= 0 ? loading : current;
}

static inline int ks_fixable(u32 cr2, u32 priv_len)
{
    if (!priv_len) return 0;
    if (!ks_in_priv(cr2)) return 0;
    u32 mapped = (priv_len + 0xFFFu) & ~0xFFFu;
    return cr2 - KEXT_PRIV_VA < mapped;
}

static inline int ks_fixable_slot(int slot, u32 cr2, u32 priv_len)
{
    if (slot < 0 || !priv_len) return 0;
    if (ks_space_of(cr2) != slot) return 0;
    u32 mapped = (priv_len + 0xFFFu) & ~0xFFFu;
    return cr2 - ks_space_base(slot) < mapped;
}
