/* Sets framebuffer memory to combine consecutive writes. */
#include "os.h"
#include "mtrr.inc"

static int have_mtrr;
static int have_fixed;
static int nvar;
static int applied;

static inline void msr_read(u32 msr, u32 *lo, u32 *hi)
{
    __asm__ volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

static inline void msr_write(u32 msr, u32 lo, u32 hi)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

static int mtrr_probe(void)
{
    u32 r[4];
    cpuid_raw(0, r);
    if (r[0] == 0) return 0;
    cpuid_raw(1, r);
    if (!(r[3] & (1u << 5)))  return 0;
    if (!(r[3] & (1u << 12))) return 0;

    u32 lo, hi;
    msr_read(MSR_MTRRCAP, &lo, &hi);
    nvar       = (int)MTRRCAP_VCNT(lo);
    have_fixed = (lo & MTRRCAP_FIX) != 0;
    if (!(lo & MTRRCAP_WC)) return 0;
    return nvar > 0;
}

static u32 saved_cr0, saved_def_lo, saved_def_hi, saved_flags;

static void mtrr_begin(void)
{

    __asm__ volatile("pushfl\n\tpopl %0" : "=r"(saved_flags) :: "memory");
    __asm__ volatile("cli");
    __asm__ volatile("mov %%cr0, %0" : "=r"(saved_cr0));

    u32 cd = (saved_cr0 | (1u << 30)) & ~(1u << 29);
    __asm__ volatile("mov %0, %%cr0" :: "r"(cd) : "memory");
    __asm__ volatile("wbinvd" ::: "memory");
    msr_read(MSR_MTRR_DEF_TYPE, &saved_def_lo, &saved_def_hi);
    msr_write(MSR_MTRR_DEF_TYPE, saved_def_lo & ~DEFTYPE_E, saved_def_hi);
}

static void mtrr_end(u32 def_lo)
{
    msr_write(MSR_MTRR_DEF_TYPE, def_lo | DEFTYPE_E, saved_def_hi);
    __asm__ volatile("wbinvd" ::: "memory");
    paging_flush_all();
    __asm__ volatile("mov %0, %%cr0" :: "r"(saved_cr0) : "memory");
    if (saved_flags & 0x200u) __asm__ volatile("sti");
}

static u32 mask_to_size(u32 mlo)
{
    u32 mask = mlo & 0xFFFFF000u;
    return mask ? (~mask & 0xFFFFF000u) + 4096 : 0;
}

static int count_claims(u32 base, u32 size, int *first, u32 *type)
{
    int n = 0;
    for (int i = 0; i < nvar; i++) {
        u32 blo, bhi, mlo, mhi;
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2, &blo, &bhi);
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2 + 1, &mlo, &mhi);
        if (!(mlo & PHYSMASK_VALID)) continue;
        u32 sz = mask_to_size(mlo);
        if (!sz) continue;
        if (!mtrr_overlap(base, size, blo & 0xFFFFF000u, sz)) continue;
        if (n == 0) { if (first) *first = i; if (type) *type = blo & 0xFF; }
        n++;
    }
    return n;
}

static void write_range(int slot, u32 base, u32 size, u32 type)
{

    msr_write(MSR_MTRR_PHYSBASE0 + slot * 2, (base & 0xFFFFF000u) | type, 0);
    msr_write(MSR_MTRR_PHYSBASE0 + slot * 2 + 1,
              mtrr_physmask(size) | PHYSMASK_VALID, 0);
}

static int find_free_slot(int from)
{
    for (int i = from; i < nvar; i++) {
        u32 lo, hi;
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2 + 1, &lo, &hi);
        if (!(lo & PHYSMASK_VALID)) return i;
    }
    return -1;
}

static int wc_add(const MtrrRange *plan, int n)
{
    int slot[MTRR_MAXPLAN], next = 0;
    for (int i = 0; i < n; i++) {
        slot[i] = find_free_slot(next);
        if (slot[i] < 0) {
            klog("mtrr: no free range register - fb left uncached\n");
            return 0;
        }
        next = slot[i] + 1;
    }
    mtrr_begin();
    for (int i = 0; i < n; i++)
        write_range(slot[i], plan[i].base, plan[i].size, MT_WC);
    mtrr_end(saved_def_lo);
    return n;
}

static int wc_takeover(int claim, u32 wb_size, const MtrrRange *plan, int n)
{
    int need = 1 + n;
    int slot[MTRR_MAXPLAN + 1], ns = 0;
    slot[ns++] = claim;
    for (int i = 0; i < nvar && ns < need; i++) {
        if (i == claim) continue;
        u32 lo, hi;
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2 + 1, &lo, &hi);
        if (!(lo & PHYSMASK_VALID)) slot[ns++] = i;
    }
    if (ns < need) {
        klog("mtrr: too few range registers to retype - fb left uncached\n");
        return 0;
    }

    mtrr_begin();
    write_range(slot[0], 0, wb_size, MT_WB);
    for (int i = 0; i < n; i++)
        write_range(slot[1 + i], plan[i].base, plan[i].size, MT_WC);

    mtrr_end((saved_def_lo & ~0xFFu) | MT_UC);
    return n;
}

int mtrr_wc_range(u32 base, u32 len)
{
    if (!have_mtrr) return 0;

    MtrrRange plan[MTRR_MAXPLAN];
    int n = mtrr_plan(base, len, plan, MTRR_MAXPLAN);
    if (n <= 0) {
        klog("mtrr: cannot express fb range - left uncached\n");
        return 0;
    }
    u32 span = 0;
    for (int i = 0; i < n; i++) span += plan[i].size;

    int claim = -1;
    u32 claim_type = 0;
    int nclaims = count_claims(base, span, &claim, &claim_type);

    if (nclaims == 0) {
        applied = wc_add(plan, n);
        return applied;
    }

    char b[76];
    u32 def_lo, def_hi, wb_size = 0;
    msr_read(MSR_MTRR_DEF_TYPE, &def_lo, &def_hi);
    if (nclaims == 1 &&
        mtrr_takeover_ok(def_lo & 0xFF, claim_type, BOOTINFO->mem_kb, &wb_size)) {
        kfmt(b, sizeof b, "mtrr: retyping map, RAM %x WB, was default %u\n",
             wb_size, def_lo & 0xFF);
        klog(b);
        applied = wc_takeover(claim, wb_size, plan, n);
        return applied;
    }
    kfmt(b, sizeof b, "mtrr: fb inside %d range(s), [%d] type %u - uncached\n",
         nclaims, claim, claim_type);
    klog(b);
    return 0;
}

int mtrr_wc_vga_window(void)
{
    if (!have_mtrr || !have_fixed) return 0;
    u32 lo, hi;
    msr_read(MSR_MTRR_DEF_TYPE, &lo, &hi);

    if (!(lo & DEFTYPE_FE)) {
        klog("mtrr: fixed ranges off - VGA window left uncached\n");
        return 0;
    }
    mtrr_begin();
    msr_write(MSR_MTRRFIX16K_A0000,
              (MT_WC << 24) | (MT_WC << 16) | (MT_WC << 8) | MT_WC,
              (MT_WC << 24) | (MT_WC << 16) | (MT_WC << 8) | MT_WC);
    mtrr_end(saved_def_lo);
    return 1;
}

static void mtrr_dump(u32 fb_base, u32 fb_len)
{
    char b[80];
    u32 lo, hi;
    msr_read(MSR_MTRR_DEF_TYPE, &lo, &hi);
    kfmt(b, sizeof b, "mtrr: %d ranges, default %u%s\n", nvar, lo & 0xFF,
         (lo & DEFTYPE_E) ? ", on" : ", OFF");
    klog(b);
    kfmt(b, sizeof b, "mtrr: fb %x len %x\n", fb_base, fb_len);
    klog(b);
    for (int i = 0; i < nvar; i++) {
        u32 blo, bhi, mlo, mhi;
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2, &blo, &bhi);
        msr_read(MSR_MTRR_PHYSBASE0 + i * 2 + 1, &mlo, &mhi);
        if (!(mlo & PHYSMASK_VALID)) continue;
        kfmt(b, sizeof b, "mtrr: [%d] %x size %x type %u\n", i,
             blo & 0xFFFFF000u, mask_to_size(mlo), blo & 0xFF);
        klog(b);
    }
}

void mtrr_init(u32 fb_base, u32 fb_len, int banked)
{
    have_mtrr = mtrr_probe();
    if (!have_mtrr) {
        klog("mtrr: not available - framebuffer stays uncached\n");
        return;
    }
    mtrr_dump(fb_base, fb_len);
    if (banked) {
        if (mtrr_wc_vga_window()) klog("mtrr: VGA window write-combining\n");
        return;
    }

    if (fb_base < 0x100000) {
        klog("mtrr: framebuffer below 1MB - left uncached\n");
        return;
    }
    if (mtrr_wc_range(fb_base, fb_len) > 0) {

        paging_fb_write_combine();
        klog("mtrr: framebuffer write-combining\n");
    }
}

int mtrr_active(void) { return applied; }
