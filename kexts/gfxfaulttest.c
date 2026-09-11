/* Injects graphics faults on disposable test images. */
#include "kapi.h"
#include "gdi.h"
#include "g3d.h"
static const Kapi *api;
static void serial(const char *s) {
    for (; *s; s++) { while (!(api->inb(0x3fd)&32)) {} api->outb(0x3f8,(u8)*s); }
}
#ifdef PANIC_TEST

#include "../out/panic_addrs.h"
static u32 panic_due;
static void panic_irq(void) {
    if (*api->ticks < panic_due) return;
    int thread = *(volatile int *)TEST_THR_SELF;
    if (((volatile int *)TEST_FAULT_ARMED)[thread]) return;
    __asm__ volatile("movl $0xdead0000,%%eax; movl (%%eax),%%eax" ::: "eax","memory");
}
#endif
static void command(const char *args) {
#ifdef PANIC_TEST
    if (!api->strcmp(args,"panic")) {
        panic_due=*api->ticks+100;
        api->irq_register(0,panic_irq);return;
    }
#endif
    if (!api->strcmp(args,"null")) {
        __asm__ volatile("xorl %%eax,%%eax; movl (%%eax),%%eax" ::: "eax","memory");
        return;
    }
    if (!api->strcmp(args,"status")) {
        char b[100];
        int gd=0,g3=0;
        for(int i=0;i<api->kext_count();i++) {
            const KextInfo *k=api->kext_get(i);
            if (!api->strcmp(k->name,"sys/gdi.kx")) gd=k->status;
            if (!api->strcmp(k->name,"sys/g3d.kx")) g3=k->status;
        }
        api->kfmt(b,sizeof b,"GFXCHECK gdi=%d g3d=%d status=%d,%d faults=%d\n",
            api->service_get("gdi")!=0,api->service_get("g3d")!=0,gd,g3,api->fault_count());
        serial(b);api->shell_print(b);return;
    }
    if (!api->strcmp(args,"g3d-api")) {
        const G3dOps *g=g3d_bind(api,3);
        if(g)for(int i=0;i<4;i++)api->mem_poke((u32)&g->create+i,0);
        api->shell_print("Incomplete G3D API armed.\n");return;
    }
    u32 target=0;
    if (!api->strcmp(args,"gdi")) {
        const GdiOps *g=gdi_bind(api,11);
        if(g)target=(u32)g->fill_poly_aa;
    } else if (!api->strcmp(args,"g3d")) {
        const G3dOps *g=g3d_bind(api,3);
        if(g)target=(u32)g->create;
    } else if (!api->strcmp(args,"background")) {
        const GdiOps *g=gdi_bind(api,11);
        if(g)target=(u32)g->fill_gradient;
    }
    if(!target){api->shell_print("No target\n");return;}
    const u8 code[6]={0xa1,0,0,0,0,0xc3};
    for(int i=0;i<6;i++)if(!api->mem_poke(target+i,code[i])) {
        api->shell_print("Patch failed\n");return;
    }
    api->shell_print("Fault armed; open GFX Demo.\n");
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Graphics Probe"};
int kext_entry(const Kapi *k) {
    if(k->version<KAPI_VERSION)return 1;
    api=k;
    return k->register_cmd("gfxcheck","gfxcheck gdi|g3d|background|null|status",command)<0;
}
