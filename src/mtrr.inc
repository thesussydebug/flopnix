/* Calculates framebuffer write-combining ranges. */
enum { MT_UC = 0, MT_WC = 1, MT_WT = 4, MT_WP = 5, MT_WB = 6 };

#define MSR_MTRRCAP          0x0FE
#define MSR_MTRR_DEF_TYPE    0x2FF
#define MSR_MTRR_PHYSBASE0   0x200
#define MSR_MTRRFIX16K_A0000 0x259

#define MTRRCAP_VCNT(c) ((c) & 0xFF)
#define MTRRCAP_FIX     (1u << 8)
#define MTRRCAP_WC      (1u << 10)
#define DEFTYPE_FE      (1u << 10)
#define DEFTYPE_E       (1u << 11)
#define PHYSMASK_VALID  (1u << 11)

#define MTRR_MAXPLAN 8

typedef struct { u32 base, size; } MtrrRange;

static u32 mtrr_pow2_up(u32 n)
{
    if (n == 0) return 0;
    u32 p = 1;
    while (p < n) {
        if (p > 0x40000000u) return 0;
        p <<= 1;
    }
    return p;
}

static u32 mtrr_physmask(u32 size)
{
    if (size < 4096) return 0;
    return (~(size - 1)) & 0xFFFFF000u;
}

static int mtrr_plan(u32 base, u32 len, MtrrRange *out, int max)
{
    if (len == 0) return 0;
    if (base & 0xFFFu) return -1;

    len = mtrr_pow2_up(len);
    if (len == 0) return -1;
    if (len < 4096) len = 4096;

    int n = 0;
    while (len) {

        u32 blk = base ? (base & (0u - base)) : 0x80000000u;
        while (blk > len) blk >>= 1;
        if (blk < 4096) return -1;
        if (n >= max) return -1;
        out[n].base = base;
        out[n].size = blk;
        n++;
        base += blk;
        len -= blk;
    }
    return n;
}

static int mtrr_takeover_ok(u32 def_type, u32 claim_type, u32 ram_kb,
                            u32 *wb_size)
{
    if (def_type != MT_WB) return 0;
    if (claim_type != MT_UC) return 0;
    if (ram_kb == 0 || ram_kb > 0x3FFFFFu) return 0;

    u32 ram = ram_kb * 1024u;
    u32 wb  = mtrr_pow2_up(ram);
    if (wb == 0) return 0;
    if (wb < 0x400000u) return 0;

    if (wb - ram > wb / 16u) return 0;

    if (wb_size) *wb_size = wb;
    return 1;
}

static int mtrr_overlap(u32 ab, u32 asz, u32 bb, u32 bsz)
{
    if (!asz || !bsz) return 0;
    u32 aend = ab + asz, bend = bb + bsz;
    if (aend < ab || bend < bb) return 1;
    return ab < bend && bb < aend;
}
