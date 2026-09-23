/* Maps memory and protects kernel code and private app data. */
#include "os.h"
#include "paging.inc"
#include "kextspace.inc"

static u32 page_dir[PG_NPDE]  __attribute__((aligned(4096)));
static u32 page_tab0[PG_NPTE] __attribute__((aligned(4096)));
static u32 page_tab1[PG_NPTE] __attribute__((aligned(4096)));
static u32 arena_tab[PG_NPTE] __attribute__((aligned(4096)));
static u32 kext_tab[PG_NPTE]  __attribute__((aligned(4096)));
static u32 *arena_pt;
static u32 arena_slot;

static int paging_on;
static int have_pge;
static void page_load(u32 address)
{
    extern u32 panic_tss[];
    panic_tss[7]=address;
    __asm__ volatile("mov %0, %%cr3" :: "r"(address) : "memory");
}
static u32 fb_pde_first, fb_pde_count;
int paging_active(void) { return paging_on; }

static int have_pse(void)
{
    u32 r[4];
    cpuid_raw(0, r);
    if (r[0] == 0) return 0;
    cpuid_raw(1, r);
    have_pge = (r[3] & (1u << 13)) != 0;
    return (r[3] & (1u << 3)) != 0;
}

static u32 gflag(void) { return have_pge ? PG_GLOBAL : 0; }

static void map4m(u32 base, u32 len, u32 flags)
{
    u32 first, count;
    if (!pg_span4m(base, len, &first, &count)) return;
    for (u32 i = 0; i < count && first + i < PG_NPDE; i++)
        page_dir[first + i] = pde_4m((first + i) << 22, flags);
}

void paging_init(void)
{
    if (!have_pse()) {
        klog("paging: no PSE - running unpaged\n");
        return;
    }

    for (u32 i = 0; i < PG_NPDE; i++) page_dir[i] = 0;

    for (u32 i = 0; i < PG_NPTE; i++)
        page_tab0[i] = i ? pte_4k(i << 12, PG_RW | gflag()) : 0;
    page_dir[0] = pde_table((u32)page_tab0, PG_RW);

    u32 ram = BOOTINFO->mem_kb ? BOOTINFO->mem_kb * 1024u : 16u * 1024 * 1024;
    if (ram > 0xF0000000u) ram = 0xF0000000u;
    if (ram > PG_4M) map4m(PG_4M, ram - PG_4M, PG_RW | gflag());

    arena_slot = ARENA_BASE & ~(PG_4M - 1);
    arena_pt = arena_slot ? arena_tab : page_tab0;
    if (arena_slot) {
        for (u32 i = 0; i < PG_NPTE; i++)
            arena_pt[i] = pte_4k(arena_slot + (i << 12), PG_RW | gflag());
        page_dir[arena_slot >> 22] = pde_table((u32)arena_pt, PG_RW);
    }

    u32 slot = KEXT_POOL_BASE & ~(PG_4M - 1);
    u32 *pool_pt = !slot ? page_tab0 : slot == arena_slot ? arena_pt : page_tab1;
    if (pool_pt == page_tab1) {
        for (u32 i = 0; i < PG_NPTE; i++)
            pool_pt[i] = pte_4k(slot + (i << 12), PG_RW | gflag());
        page_dir[slot >> 22] = pde_table((u32)pool_pt, PG_RW);
    }
    if (KEXT_PRIVATE_ENABLED)
        for (u32 pa = KEXT_POOL_BASE; pa < KEXT_POOL_END; pa += PG_4K)
            pool_pt[(pa - slot) >> 12] = 0;

    u32 fb = BOOTINFO->lfb;
    u32 fblen = (u32)BOOTINFO->pitch * (u32)BOOTINFO->h;

    if (fb >= PG_4M && fblen) {
        map4m(fb, fblen, PG_RW | PG_PCD | gflag());
        pg_span4m(fb, fblen, &fb_pde_first, &fb_pde_count);
    }

    page_load((u32)page_dir);

    u32 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 | (1u << 4) |
                                       (have_pge ? (1u << 7) : 0)) : "memory");

    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0"
                     :: "r"(cr0 | 0x80000000u | (1u << 16)) : "memory");

    paging_on = 1;
    klog("paging: on, identity mapped, null page trapped\n");
}

void paging_map_kext(u32 phys, u32 len)
{
    static u32 mapped;
    if (!paging_on) return;
    u32 pages = (len + 0xFFF) >> 12;
    if (pages > PG_NPTE) pages = PG_NPTE;

    for (u32 i = 0; i < pages; i++)
        kext_tab[i] = pte_4k(phys + (i << 12), PG_RW);
    for (u32 i = pages; i < mapped; i++)
        kext_tab[i] = 0;
    mapped = pages;
    page_dir[KEXT_PRIV_PDE] = pde_table((u32)kext_tab, PG_RW);
    paging_flush();
}

static u32 kext_pd[KEXT_PD_MAX][PG_NPDE] __attribute__((aligned(4096)));
static u32 kext_pt[KEXT_PD_MAX][PG_NPTE] __attribute__((aligned(4096)));
static u8  pd_live[KEXT_PD_MAX];
static int pd_current = -1;

int paging_space_create(int slot, u32 phys, u32 len)
{
    if (!paging_on || slot < 0 || slot >= KEXT_PD_MAX) return 0;
    u32 pages = (len + 0xFFF) >> 12;
    if (pages > PG_NPTE) pages = PG_NPTE;

    for (u32 i = 0; i < PG_NPDE; i++) kext_pd[slot][i] = page_dir[i];

    for (int s = 0; s < KEXT_PD_MAX; s++) kext_pd[slot][ks_space_pde(s)] = 0;

    for (u32 i = 0; i < PG_NPTE; i++)
        kext_pt[slot][i] = i < pages ? pte_4k(phys + (i << 12), PG_RW) : 0;
    kext_pd[slot][ks_space_pde(slot)] = pde_table((u32)kext_pt[slot], PG_RW);
    pd_live[slot] = 1;
    return 1;
}

void paging_space_switch(int slot)
{
    if (!paging_on) return;
    if (slot >= KEXT_PD_MAX || (slot >= 0 && !pd_live[slot])) slot = -1;
    if (slot == pd_current) return;
    pd_current = slot;
    u32 cr3 = slot < 0 ? (u32)page_dir : (u32)kext_pd[slot];
    page_load(cr3);
}

int paging_space_current(void) { return pd_current; }
void paging_space_drop(int slot)
{
    if(slot<0||slot>=KEXT_PD_MAX)return;
    if(pd_current==slot)paging_space_switch(-1);
    memset(kext_pt[slot],0,sizeof kext_pt[slot]);pd_live[slot]=0;
}

void paging_space_sync(void)
{
    for (int s = 0; s < KEXT_PD_MAX; s++) {
        if (!pd_live[s]) continue;
        for (u32 i = 0; i < PG_NPDE; i++) {
            if (i >= ks_space_pde(0) && i < ks_space_pde(0) + KEXT_PD_MAX) continue;
            kext_pd[s][i] = page_dir[i];
        }
    }
    paging_flush_all();
}

void paging_unmap_kext(void)
{
    if (!paging_on) return;
    page_dir[KEXT_PRIV_PDE] = 0;
    paging_flush();
}

void paging_arena_protect(u32 lo, u32 hi, int writable)
{
    if (!paging_on) return;
    u32 first, count;
    if (!pg_pte_span(arena_slot, lo, hi, &first, &count)) return;
    for (u32 i = 0; i < count && first + i < PG_NPTE; i++) {
        if (writable) arena_pt[first + i] |=  (u32)PG_RW;
        else          arena_pt[first + i] &= ~(u32)PG_RW;
    }
    paging_flush_all();
}

void paging_set_user(u32 va, u32 npages, int user)
{
    if (!paging_on) return;
    if (va + npages * PG_4K > PG_4M) return;
    u32 first = va >> 12;
    for (u32 i = 0; i < npages && first + i < PG_NPTE; i++) {
        if (user) page_tab0[first + i] |=  (u32)(PG_USER | PG_RW);
        else      page_tab0[first + i] &= ~(u32)PG_USER;
    }
    if (user) {
        page_dir[0] |= (u32)PG_USER;

        paging_space_sync();
    }
    paging_flush_all();
}

void paging_fb_write_combine(void)
{
    if (!paging_on || !fb_pde_count) return;
    for (u32 i = 0; i < fb_pde_count && fb_pde_first + i < PG_NPDE; i++)
        page_dir[fb_pde_first + i] &= ~(u32)PG_PCD;

    paging_space_sync();
    paging_flush_all();
}

int paging_mapped(u32 va)
{
    if (!paging_on) return 1;

    const u32 *pd = pd_current < 0 ? page_dir : kext_pd[pd_current];
    u32 pde = pd[pd_index(va)];
    u32 pte = 0;
    if ((pde & PG_PRESENT) && !(pde & PG_PS)) {
        const u32 *pt = (const u32 *)(pde & 0xFFFFF000u);
        pte = pt[pt_index(va)];
    }
    return pg_va_mapped(pde, pte);
}

int paging_writable(u32 va)
{
    if (!paging_on) return 1;
    const u32 *pd = pd_current < 0 ? page_dir : kext_pd[pd_current];
    u32 pde = pd[pd_index(va)];
    if (!(pde & PG_PRESENT)) return 0;
    if (pde & PG_PS) return (pde & PG_RW) != 0;
    const u32 *pt = (const u32 *)(pde & 0xFFFFF000u);
    u32 pte = pt[pt_index(va)];
    return (pte & PG_PRESENT) && (pte & PG_RW) && (pde & PG_RW);
}

int mem_poke(u32 va, u8 val)
{
    if (!paging_mapped(va)) return 0;
    u32 f = irq_save();
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0 & ~(1u << 16)) : "memory");
    *(volatile u8 *)va = val;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    irq_restore(f);
    return 1;
}

u32 page_fault_addr(void)
{
    u32 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void paging_flush(void)
{
    if (!paging_on) return;

    u32 cr3 = pd_current < 0 ? (u32)page_dir : (u32)kext_pd[pd_current];
    page_load(cr3);
}

void paging_flush_all(void)
{
    if (!paging_on) return;
    if (!have_pge) { paging_flush(); return; }
    u32 cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 & ~(1u << 7)) : "memory");
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
}

u32 paging_pages_mapped(void)
{
    if (!paging_on) return 0;

    const u32 *pd = pd_current < 0 ? page_dir : kext_pd[pd_current];
    u32 n = 0;
    for (int i = 0; i < PG_NPDE; i++) {
        u32 pde = pd[i];
        if (!(pde & PG_PRESENT)) continue;
        if (pde & PG_PS) { n += PG_NPTE; continue; }
        const u32 *pt = (const u32 *)(pde & 0xFFFFF000u);
        for (int j = 0; j < PG_NPTE; j++)
            if (pt[j] & PG_PRESENT) n++;
    }
    return n;
}
