/* Counts CPU time for threads and app callbacks. */
#include "os.h"
#define CA_APPS MAX_APPS
#define CA_THREADS THR_MAX
#include "cpuaccount.inc"
static CpuAccount account = { .owner = {-1,-1,-1,-1,-1,-1,-1,-1} };
static u32 pct[MAX_APPS + 1], win_start;
u32 cpu_now(void) { u32 lo; __asm__ volatile("rdtsc" : "=a"(lo) :: "edx"); return lo; }
static u32 cpu_stamp(void) {
    u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ca_stamp(lo, hi);
}
int cpu_context(int type) {
    u32 f = irq_save();
    int old = ca_owner(&account, cpu_stamp(), type);
    irq_restore(f); return old;
}
void cpu_thread_switch(int next) { ca_switch(&account, cpu_stamp(), next); }
void cpu_snapshot(void) {
    u32 f = irq_save(), now = cpu_stamp(), span = now - win_start;
    if (span < 1000) { irq_restore(f); return; }
    ca_step(&account, now);
    u32 unit = span / 1000, busy = 0;
    for (int i = 0; i < MAX_APPS; i++) {
        u32 n = account.acc[i] / unit;
        pct[i] = n > 1000 ? 1000 : n;
        busy += pct[i]; account.acc[i] = 0;
    }
    pct[MAX_APPS] = busy < 1000 ? 1000 - busy : 0;
    win_start = now; irq_restore(f);
}
u32 cpu_usage(int type) {
    if (type < 0) return pct[MAX_APPS];
    return type < MAX_APPS ? pct[type] : 0;
}
