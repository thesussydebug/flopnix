/* Injects crashes on disposable test images. */
#include "kapi.h"
#include "../out/emergency_test_addrs.inc"
static const Kapi *api;
static int mode, ran;
static void serial(const char *s)
{
    for (;*s;s++) {
        for (int n=0;n<10000;n++) if (api->inb(0x3fd)&0x20) break;
        api->outb(0x3f8,(u8)*s);
    }
}
static void patch(u32 addr,int loop)
{
    u32 flags; __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    api->mem_poke(addr,loop ? 0xeb : 0x0f); api->mem_poke(addr+1,loop ? 0xfe : 0x0b);
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory");
}
static void run(void *ctx)
{
    (void)ctx;
    if (ran++) return;
    serial("PANICTEST RUN\n");
    if (mode==2 || mode==3) patch((u32)api->kfmt,mode==3);
    if (mode==8) {
        u32 cr3,cr4; __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
        u32 fb=*(volatile u32 *)(0x7000+8);
        ((volatile u32 *)cr3)[fb>>22]=0;
        __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
        __asm__ volatile("mov %0,%%cr4" :: "r"(cr4&~128u) : "memory");
        __asm__ volatile("mov %0,%%cr3" :: "r"(cr3) : "memory");
    }
    if (mode==11) *(volatile int *)EM_TIMER_ADDR=0;
    if (mode==12) {
        __asm__ volatile("cli");
        patch((u32)api->kfmt,0);
        *(volatile int *)EM_ARM_ADDR=0;
        *(volatile u32 *)0xdead0000u=1;
    }
    if (mode==10) *(volatile int *)api->screen_w=0;
    if (mode==9) { *(volatile u32 *)EM_CHECK_ADDR^=1; patch((u32)api->kfmt,0); }
    if (mode==4) patch((u32)api->fill_rect,0);
    if (mode==5) { __asm__ volatile("cli\nxorl %esp,%esp\nud2"); __builtin_unreachable(); }
    if (mode==6 || mode==7) {
        ((void(*)(void))EM_HEART_ADDR)();
        if (mode==7) __asm__ volatile("cli");
        else __asm__ volatile("sti");
        for (;;) __asm__ volatile("pause");
    }
    ((void(*)(u32,u32,u32))PANIC_ADDR)(6,0,(u32)run);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Emergency test"};
int kext_entry(const Kapi *k)
{
    api=k;
    u8 b=0;
    if (k->fs_read("panic.case",&b,1)!=1 || b<1 || b>12) return 1;
    mode=b;
    k->outb(0x3f9,0);k->outb(0x3fb,0x80);k->outb(0x3f8,1);k->outb(0x3f9,0);
    k->outb(0x3fb,3); k->outb(0x3fa,7);k->outb(0x3fc,3);
    serial("PANICTEST ARMED\n");
    k->timer_add(600,run,0);
    return 0;
}
