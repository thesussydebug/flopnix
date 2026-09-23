/* Sets up CPU privilege levels and the double-fault task. */
#include "os.h"
#include "paging.inc"
#include "ring3.inc"

typedef struct __attribute__((packed)) { u16 limit; u32 base; } Gdtr;

static unsigned long long gdt[GDT_ENTS];
static Gdtr gdtr;

typedef struct __attribute__((packed)) {
    u32 prev;
    u32 esp0, ss0, esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} Tss;

Tss panic_tss;
static Tss df_tss;
extern u8 emergency_stack[8192];
extern u32 emergency_pd[1024];
extern void emergency_df_entry(void);
__attribute__((noreturn)) void emergency_double_fault(void)
{
    u32 cr2; __asm__ volatile("mov %%cr2,%0" : "=r"(cr2));
    emergency_enter(8,0,panic_tss.eip,cr2,EM_DOUBLE);
}

static u8 r0_stack[8192] __attribute__((aligned(16)));

extern void gdt_load(void *);
extern void tss_load(u32 sel);
extern void isr128(void);
void idt_set_user_gate(int vec, u32 handler);

static int r3_ready;

void ring3_init(void)
{
    gdt[0] = 0;
    gdt[SEL_KCODE / 8] = r3_desc(0, 0xFFFFF, ACC_KCODE, GRAN_4G);
    gdt[SEL_KDATA / 8] = r3_desc(0, 0xFFFFF, ACC_KDATA, GRAN_4G);
    gdt[SEL_UCODE / 8] = r3_desc(0, 0xFFFFF, ACC_UCODE, GRAN_4G);
    gdt[SEL_UDATA / 8] = r3_desc(0, 0xFFFFF, ACC_UDATA, GRAN_4G);
    gdt[SEL_TSS / 8]   = r3_desc((u32)&panic_tss, sizeof panic_tss - 1, ACC_TSS, GRAN_BYTE);

    memset(&df_tss,0,sizeof df_tss);
    df_tss.cr3=(u32)emergency_pd; df_tss.eip=(u32)emergency_df_entry;
    df_tss.eflags=2; df_tss.esp=(u32)emergency_stack+sizeof emergency_stack;
    df_tss.cs=SEL_KCODE; df_tss.ss=df_tss.ds=df_tss.es=df_tss.fs=df_tss.gs=SEL_KDATA;
    df_tss.iomap=sizeof df_tss;
    gdt[SEL_DFTSS/8]=r3_desc((u32)&df_tss,sizeof df_tss-1,ACC_TSS,GRAN_BYTE);
    gdtr.limit = sizeof gdt - 1;
    gdtr.base  = (u32)gdt;
    gdt_load(&gdtr);

    memset(&panic_tss, 0, sizeof panic_tss);
    __asm__ volatile("mov %%cr3,%0":"=r"(panic_tss.cr3));
    panic_tss.ss0   = SEL_KDATA;
    panic_tss.esp0  = (u32)r0_stack + sizeof r0_stack;
    panic_tss.iomap = sizeof panic_tss;
    tss_load(SEL_TSS);
    idt_set_task_gate(8,SEL_DFTSS);

    idt_set_user_gate(0x80, (u32)isr128);
    r3_ready = 1;
    klog("ring3: gdt 7 entries, tss loaded, syscall gate int 80h (dpl 3)\n");
}

int ring3_active(void) { return r3_ready; }

typedef struct { u32 edi, esi, ebp, esp, ebx, edx, ecx, eax; } Regs;

enum { SYS_EXIT = 0, SYS_PING = 1 };

static JmpBuf r3_exit;
static volatile int r3_running;
static volatile u32 r3_result;

void syscall_dispatch(Regs *r)
{
    switch (r->eax) {
    case SYS_PING:

        r->eax = r->ebx + 1;
        break;
    case SYS_EXIT:
        if (r3_running) {
            r3_result = r->ebx;
            r3_running = 0;
            fj_long(&r3_exit, 1);
        }
        break;
    default:
        r->eax = (u32)-1;
        break;
    }
}

int ring3_fault(u32 vec, u32 err)
{
    if (!r3_running) return 0;
    r3_running = 0;
    r3_result = (vec << 16) | (err & 0xFFFF);
    fj_long(&r3_exit, 2);
}

static void enter_user(u32 eip, u32 esp)
{
    __asm__ volatile(
        "pushl %2\n\t"
        "pushl %1\n\t"
        "pushl %3\n\t"
        "pushl %4\n\t"
        "pushl %0\n\t"
        "iret\n\t"
        :: "r"(eip), "r"(esp), "r"(SEL_USER(SEL_UDATA)), "r"(R3_EFLAGS),
           "r"(SEL_USER(SEL_UCODE))
        : "memory");
}

static void reload_kernel_segments(void)
{
    __asm__ volatile(
        "mov %0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :: "i"(SEL_KDATA) : "eax", "memory");
}

#define UPAGES 2
static u8 user_area[UPAGES * PG_4K] __attribute__((aligned(4096)));

static volatile u32 kernel_canary = 0xC0FFEE;

static const u8 pay_ping[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xBB, 0x40, 0x02, 0x00, 0x00,
    0xCD, 0x80,
    0x89, 0xC3,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80
};

static const u8 pay_write[] = {
    0xC7, 0x05, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80
};

static const u8 pay_io[] = {
    0xE4, 0x60,
    0xB8, 0x00, 0x00, 0x00, 0x00,
    0xCD, 0x80
};

static void put32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static u32 run_payload(const u8 *code, u32 len, int *faulted)
{
    memset(user_area, 0, sizeof user_area);
    memcpy(user_area, code, len);

    paging_set_user((u32)user_area, UPAGES, 1);

    preempt_disable();
    r3_running = 1;
    r3_result = 0;
    int how = fj_set(&r3_exit);
    if (how == 0)
        enter_user((u32)user_area, (u32)user_area + sizeof user_area - 16);

    reload_kernel_segments();
    paging_set_user((u32)user_area, UPAGES, 0);
    r3_running = 0;
    preempt_enable();
    if (faulted) *faulted = (how == 2);
    return r3_result;
}

u32 ring3_selftest(int what)
{
    if (!r3_ready) return 0;
    int faulted = 0;
    u32 r;
    switch (what) {
    case 0:
        r = run_payload(pay_ping, sizeof pay_ping, &faulted);
        break;
    case 1: {
        u8 code[sizeof pay_write];
        memcpy(code, pay_write, sizeof code);
        put32(code + 2, (u32)&kernel_canary);
        kernel_canary = 0xC0FFEE;
        r = run_payload(code, sizeof code, &faulted);

        if (!faulted || kernel_canary != 0xC0FFEE) r |= 0x40000000u;
        break;
    }
    case 2:
        r = run_payload(pay_io, sizeof pay_io, &faulted);
        break;
    default:
        return 0;
    }
    return r | (faulted ? 0x80000000u : 0);
}
