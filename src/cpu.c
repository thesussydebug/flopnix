/* Detects the CPU and measures its clock speed. */
#include "os.h"

static char brand[40] = "unknown CPU";
static u32  mhz;

static inline int has_cpuid(void)
{
    u32 r;
    __asm__ volatile(
        "pushfl\n\t pop %%eax\n\t mov %%eax,%%ecx\n\t xor $0x200000,%%eax\n\t"
        "push %%eax\n\t popfl\n\t pushfl\n\t pop %%eax\n\t xor %%ecx,%%eax"
        : "=a"(r) :: "ecx", "cc");
    return (r & 0x200000) != 0;
}
static inline void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}
static inline u32 rdtsc_lo(void)
{
    u32 lo;
    __asm__ volatile("rdtsc" : "=a"(lo) :: "edx");
    return lo;
}

void cpu_init(void)
{
    if (!has_cpuid()) return;

    u32 a, b, c, d;
    char vend[13];
    cpuid(0, &a, &b, &c, &d);
    *(u32 *)vend = b; *(u32 *)(vend + 4) = d; *(u32 *)(vend + 8) = c;
    vend[12] = 0;

    cpuid(1, &a, &b, &c, &d);
    int fam = (a >> 8) & 0xF;
    if (fam == 0xF) fam += (a >> 20) & 0xFF;

    const char *v = "x86";
    if (!strcmp(vend, "GenuineIntel")) v = "Intel";
    else if (!strcmp(vend, "AuthenticAMD")) v = "AMD";
    else if (!strcmp(vend, "CyrixInstead")) v = "Cyrix";
    else if (!strcmp(vend, "CentaurHauls")) v = "VIA";

    const char *cls = "x86";
    if (fam == 4) cls = "486";
    else if (fam == 5) cls = "Pentium";
    else if (fam == 6) cls = "P6 (Pentium II/III)";
    else if (fam >= 15) cls = "Pentium 4 class";
    kfmt(brand, sizeof brand, "%s %s", v, cls);
    bmark('1');

    if (fam >= 5 && timer_alive) {

        u32 t0 = ticks, guard = 0;
        while ((u32)(ticks - t0) < 1 && ++guard < 100000000u) ;
        if (guard >= 100000000u) return;
        t0 = ticks;
        u32 c0 = rdtsc_lo();
        guard = 0;
        while ((u32)(ticks - t0) < 5 && ++guard < 100000000u) ;
        u32 c1 = rdtsc_lo();
        u32 dt = ticks - t0;
        if (dt) mhz = (c1 - c0) / (dt * 10000);
        bmark('2');
    }
}

const char *cpu_brand(void) { return brand; }
u32 cpu_mhz(void) { return mhz; }

void cpuid_raw(u32 leaf, u32 out[4])
{
    if (!has_cpuid()) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    cpuid(leaf, &out[0], &out[1], &out[2], &out[3]);
}

void tsc_read(u32 *hi, u32 *lo)
{
    u32 a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    if (hi) *hi = d;
    if (lo) *lo = a;
}
