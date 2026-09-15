/* Schedules threads and switches their stacks and app memory. */
#include "os.h"
#include "sched.inc"

int thr_self;

#define THR_STACK 16384

#define THR_CANARY 0xC0FFEE5Au
#define THR_FILL   0xA5A5A5A5u

typedef struct {
    u32  esp;
    u32  runs;
    int  kext;
    int creator;
    u8   state;
    char name[12];
} Thread;

static Thread thr[THR_MAX];
static u8 stacks[THR_MAX][THR_STACK] __attribute__((aligned(16)));

static int thr_on;
static int preempt;
static int nopreempt;
static int thr_overflow = -1;
static int thr_testing;

void preempt_disable(void) { u32 f = irq_save(); nopreempt++; irq_restore(f); }
void preempt_enable(void)  { u32 f = irq_save(); if (nopreempt) nopreempt--; irq_restore(f); }

int  preempt_depth(void)         { return nopreempt; }
void preempt_restore(int d)      { u32 f = irq_save(); nopreempt = d; irq_restore(f); }

static int stack_ok(int s)
{
    return s <= 0 || *(volatile u32 *)stacks[s] == THR_CANARY;
}

int thread_overflowed(void) { return thr_overflow; }

void thr_switch(u32 *save_esp, u32 load_esp);
void thr_bootstrap(void);

static int thread_state(int s) { return (s < 0 || s >= THR_MAX) ? THR_FREE : thr[s].state; }

void threads_init(void)
{
    if (thr_on) return;
    for (int i = 0; i < THR_MAX; i++) { thr[i].state = THR_FREE; thr[i].kext = -1; }
    thr[0].state = THR_RUN;
    strlcpy(thr[0].name, "main", sizeof thr[0].name);
    thr_self = 0;
    thr_on = 1;
}

int thread_create(void (*fn)(void), const char *name)
{
    if (!thr_on || !fn) return -1;
    int s = -1;
    for (int i = 1; i < THR_MAX; i++)
        if (thr[i].state == THR_FREE || thr[i].state == THR_DEAD) { s = i; break; }
    if (s < 0) return -1;

    u32 *fill = (u32 *)stacks[s];
    for (u32 i = 0; i < THR_STACK / 4; i++) fill[i] = THR_FILL;
    *(volatile u32 *)stacks[s] = THR_CANARY;
    u32 *sp = (u32 *)(stacks[s] + THR_STACK);
    *--sp = (u32)fn;
    *--sp = (u32)thr_bootstrap;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    thr[s].esp = (u32)sp;
    thr[s].creator = kext_owner_now();
    thr[s].kext = -1;
    strlcpy(thr[s].name, name ? name : "thread", sizeof thr[s].name);
    thr[s].state = THR_READY;
    if (!thr_testing) {
        char m[40];
        kfmt(m, sizeof m, "thread %d started (%s)", s, thr[s].name);
        ktrace(m);
    }
    return s;
}

static void reschedule(void)
{
    if (!thr_on) return;

    if (!stack_ok(thr_self)) {
        thr_overflow = thr_self;
        thr[thr_self].state = THR_DEAD;
        if (!thr_testing) {
            char m[56];
            kfmt(m, sizeof m, "thread %d (%s) BLEW ITS STACK - killed",
                 thr_self, thr[thr_self].name);
            klog(m); klog("\n");
            ktrace(m);
        }
    }
    unsigned char st[THR_MAX];
    for (int i = 0; i < THR_MAX; i++)
        st[i] = stack_ok(i) ? thr[i].state : THR_DEAD;
    int nxt = sched_next(st, THR_MAX, thr_self);
    if (nxt < 0 || nxt == thr_self) return;

    int prev = thr_self;
    if (thr[prev].state == THR_RUN) thr[prev].state = THR_READY;
    thr[nxt].state = THR_RUN;
    thr[nxt].runs++;

    thr[prev].kext = kext_current();
    cpu_thread_switch(nxt);
    thr_self = nxt;
    thr_switch(&thr[prev].esp, thr[nxt].esp);
    kext_enter(thr[prev].kext);
}

void thr_yield(void)
{
    if (!thr_on) return;
    u32 f = irq_save();
    reschedule();
    irq_restore(f);
}

void thr_tick(void)
{
    if (!thr_on) return;
    app_kill_poll();

    if (!thr_may_switch(preempt, fault_armed[thr_self], nopreempt, in_irq)) return;
    reschedule();
}

void thr_preempt_set(int on) { preempt = on ? 1 : 0; }
int  thr_preempt_on(void)    { return preempt; }

void thr_exit(void)
{
    irq_save();
    if (thr_on && thr_self > 0) thr[thr_self].state = THR_DEAD;
    for (;;) { reschedule(); sti(); hlt(); cli(); }
}

static volatile char tt_seq[16];
static volatile int  tt_n;

static void tt_mark(char c)
{
    if (tt_n < (int)sizeof tt_seq - 1) tt_seq[tt_n++] = c;
}
static void tt_a(void) { for (int i = 0; i < 4; i++) { tt_mark('a'); thr_yield(); } }
static void tt_b(void) { for (int i = 0; i < 4; i++) { tt_mark('b'); thr_yield(); } }

static Mutex tt_mx = MUTEX_INIT;
static volatile int tt_shared;

static void tt_smash(void)
{
    volatile u8 big[THR_STACK + 512];
    for (u32 i = 0; i < sizeof big; i++) big[i] = 1;

    thr_yield();
}

static void tt_lock(void)
{
    for (int i = 0; i < 20; i++) {
        mtx_lock(&tt_mx);
        mtx_lock(&tt_mx);
        mtx_unlock(&tt_mx);
        int v = tt_shared;
        thr_yield();
        tt_shared = v + 1;
        mtx_unlock(&tt_mx);
    }
}

static void tt_wait2(int x, int y, int budget)
{
    while (budget-- > 0 &&
           !(thread_state(x) == THR_DEAD && thread_state(y) == THR_DEAD))
        thr_yield();
}

static u32 stack_used(int s)
{
    if (s == 0) return 0;
    const u32 *w = (const u32 *)stacks[s];
    u32 i = 1;
    while (i < THR_STACK / 4 && w[i] == THR_FILL) i++;
    return THR_STACK - i * 4;
}

int thread_info(int slot, ThreadInfo *o)
{
    if (slot < 0 || slot >= THR_MAX || !o) return 0;
    o->state = thr[slot].state;
    o->runs  = thr[slot].runs;
    o->stack_used = stack_used(slot);
    o->stack_size = THR_STACK;
    strlcpy(o->name, thr[slot].state == THR_FREE ? "-" : thr[slot].name,
            sizeof o->name);
    return thr[slot].state != THR_FREE;
}

int threads_selftest(void)
{
    thr_testing = 1;
    tt_n = 0;
    int a = thread_create(tt_a, "t-a");
    int b = thread_create(tt_b, "t-b");
    if (a < 0 || b < 0) { thr_testing = 0; return 3; }
    tt_wait2(a, b, 400);
    if (thread_state(a) != THR_DEAD || thread_state(b) != THR_DEAD) { thr_testing = 0; return 1; }
    if (tt_n < 8) { thr_testing = 0; return 2; }
    int swaps = 0;
    for (int i = 1; i < tt_n; i++) if (tt_seq[i] != tt_seq[i - 1]) swaps++;
    if (swaps < 4) { thr_testing = 0; return 4; }

    tt_shared = 0;
    int c = thread_create(tt_lock, "t-c");
    int d = thread_create(tt_lock, "t-d");
    if (c < 0 || d < 0) { thr_testing = 0; return 5; }
    tt_wait2(c, d, 4000);
    if (tt_shared != 40) { thr_testing = 0; return 6; }

    int e = thread_create(tt_smash, "t-smash");
    if (e < 0) { thr_testing = 0; return 7; }
    tt_wait2(e, e, 400);
    if (thread_overflowed() != e) { thr_testing = 0; return 8; }
    thr_overflow = -1;
    thr_testing = 0;
    return 0;
}

#define MTX_HELD_MAX 8
static Mutex *mheld[THR_MAX][MTX_HELD_MAX];
static int    mheldn[THR_MAX];

void mtx_lock(Mutex *m)
{
    if (!thr_on) return;
    for (;;) {
        u32 f = irq_save();
        if (!m->held) {
            m->held = 1; m->owner = thr_self; m->depth = 1;
            if (mheldn[thr_self] < MTX_HELD_MAX)
                mheld[thr_self][mheldn[thr_self]++] = m;
            irq_restore(f); return;
        }
        if (m->owner == thr_self) {
            m->depth++;
            if (mheldn[thr_self] < MTX_HELD_MAX)
                mheld[thr_self][mheldn[thr_self]++] = m;
            irq_restore(f); return;
        }
        irq_restore(f);
        thr_yield();
    }
}

void mtx_unlock(Mutex *m)
{
    if (!thr_on) return;
    u32 f = irq_save();
    if (m->owner == thr_self) {
        if (mheldn[thr_self] > 0 &&
            mheld[thr_self][mheldn[thr_self] - 1] == m)
            mheldn[thr_self]--;
        if (--m->depth <= 0) { m->held = 0; m->owner = -1; m->depth = 0; }
    }
    irq_restore(f);
}

int mtx_held_count(void) { return thr_on ? mheldn[thr_self] : 0; }

void mtx_unwind(int snap)
{
    if (!thr_on) return;
    while (mheldn[thr_self] > snap) {
        int before = mheldn[thr_self];
        mtx_unlock(mheld[thr_self][before - 1]);
        if (mheldn[thr_self] >= before)
            mheldn[thr_self] = before - 1;
        klog("fault recovery released a leaked lock\n");
    }
}

int thread_kext_busy(int owner)
{
    for (int i=0;i<THR_MAX;i++)
        if (i!=thr_self && thr[i].state!=THR_FREE && thr[i].state!=THR_DEAD &&
            (thr[i].creator==owner || thr[i].kext==owner)) return 1;
    return 0;
}
