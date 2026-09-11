/* Calculates page-table entries and memory bounds. */
#define PG_PRESENT 0x001
#define PG_RW      0x002
#define PG_USER    0x004
#define PG_PWT     0x008
#define PG_PCD     0x010
#define PG_PS      0x080
#define PG_GLOBAL  0x100

#define PG_4K      4096u
#define PG_4M      0x400000u
#define PG_NPDE    1024
#define PG_NPTE    1024

static inline u32 pd_index(u32 va) { return va >> 22; }
static inline u32 pt_index(u32 va) { return (va >> 12) & 0x3FF; }

static inline u32 pg_down4m(u32 a) { return a & ~(PG_4M - 1); }

static inline u32 pg_up4m(u32 a)
{
    if (a > 0xFFC00000u) return 0xFFC00000u;
    return (a + PG_4M - 1) & ~(PG_4M - 1);
}

static inline u32 pde_4m(u32 phys, u32 flags)
{
    return (phys & 0xFFC00000u) | flags | PG_PS | PG_PRESENT;
}

static inline u32 pde_table(u32 pt_phys, u32 flags)
{
    return (pt_phys & 0xFFFFF000u) | flags | PG_PRESENT;
}

static inline u32 pte_4k(u32 phys, u32 flags)
{
    return (phys & 0xFFFFF000u) | flags | PG_PRESENT;
}

static inline int pg_va_mapped(u32 pde, u32 pte)
{
    if (!(pde & PG_PRESENT)) return 0;
    if (pde & PG_PS) return 1;
    return (pte & PG_PRESENT) != 0;
}

static inline int pg_pte_span(u32 slot, u32 lo, u32 hi, u32 *first, u32 *count)
{
    if (hi <= lo) return 0;
    u32 send = slot + PG_4M;
    if (lo >= send || hi <= slot) return 0;
    if (lo < slot) lo = slot;
    if (hi > send) hi = send;
    u32 a = (lo - slot) >> 12;
    u32 b = ((hi - slot) + PG_4K - 1) >> 12;
    *first = a;
    *count = b - a;
    return 1;
}

static inline int pg_span4m(u32 base, u32 len, u32 *first, u32 *count)
{
    if (len == 0) return 0;
    u32 end = base + len;
    if (end < base) return 0;
    u32 lo = pg_down4m(base);
    u32 hi = pg_up4m(end);
    if (hi <= lo) return 0;
    *first = lo >> 22;
    *count = (hi - lo) >> 22;
    return 1;
}
