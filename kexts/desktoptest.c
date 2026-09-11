/* Controls desktop tests on disposable floppy images. */
#include "kapi.h"
static const Kapi *api;
static int errors;
static void serial(const char *s)
{
    while (*s) { for (int i=0;i<10000;i++) if (api->inb(0x3fd)&32) break; api->outb(0x3f8,*s++); }
}
static void check(int ok,const char *label)
{ serial(ok ? "UITEST PASS " : "UITEST FAIL "); serial(label); serial("\n"); if (!ok) errors++; }
static int module(const char *path)
{
    for (int i=0;i<api->kext_count();i++) {
        const KextInfo *k=api->kext_get(i);
        if (k && !api->strcmp(k->name,path)) return i;
    }
    return -1;
}
static void command(const char *arg)
{
    if (!api->strcmp(arg,"check")) {
        check(module("desktop/probe.kx")<0,"desktop not loaded at boot");
        check(module("desktop/broken.kx")<0,"desktop bad ELF ignored at boot");
        check(api->app_find("Desktop Probe")<0,"desktop entry not executed");
        check(api->kext_load("desktop/probe.kx")==0,"explicit desktop load");
        int id=module("desktop/probe.kx");
        check(id>=0,"module registered"); if (id<0) return;
        u32 base=api->kext_get(id)->base,mem=api->mem_info(MI_POOL_USED);
        int count=api->kext_count();
        for (int i=0;i<16;i++) {
            check(api->kext_unload(id)==0,"idle unload");
            check(api->kext_get(id)->status==46,"unloaded state");
            check(api->cmd_usage("fixture")==0,"unloaded command unavailable");
            check(api->kext_load("desktop/probe.kx")==0,"reload");
        }
        check(api->kext_count()==count && api->kext_get(id)->base==base && api->mem_info(MI_POOL_USED)==mem,"reload does not allocate again");
        check(api->kext_unload(module("sys/desktop.kx"))==-2,"system module protected");
        check(api->kext_unload(-1)==-1,"invalid unload rejected");
        check(api->kext_unload(id)==0,"unload before open");
        check(api->open_with("desktop/probe.kx",0,0,0)==0,"desktop open reactivates and opens");
        check(api->kext_unload(id)==-3,"open window protects module");
        api->win_close_self(api->app_find("Desktop Probe"),0);
        serial("UITEST CHECK DONE\n");
    } else if (!api->strcmp(arg,"entry")) {
        check(!api->strcmp((char *)api->iobuf,"entry calls: 1"),"entry not repeated");
    } else if (!api->strcmp(arg,"background")) {
        check(api->kext_unload(module("desktop/probe.kx"))==-2,"background timer protects module");
    } else if (!api->strncmp(arg,"open ",5)) {
        int t=api->app_find(arg+5); check(t>=0 && api->win_open(t)>=0,"open requested app");
    } else if (!api->strcmp(arg,"holdcheck")) {
        serial("UITEST HOLD CHECK START\n");
        check(api->open_with("test.hold",0,0,0)==0,"arm main callback");
        volatile u8 *flag=api->iobuf;
        u32 end=*api->ticks+1400;
        while (flag[0]!=1 && (i32)(*api->ticks-end)<0) {}
        check(flag[0]==1,"main callback entered");
        check(api->kext_unload(module("desktop/probe.kx"))==-3,"paused main callback protected");
        flag[0]=2;
    } else if (!api->strcmp(arg,"finish")) {
        check(api->kext_unload(module("desktop/probe.kx"))==0,"unload after close");
        check(api->fault_count()==0,"no recovered faults");
        char b[50]; api->kfmt(b,sizeof b,"UITEST DONE: %d failures\n",errors); serial(b);
    }
}
static void size(int i,int *w,int *h) { (void)i; *w=120; *h=40; }
static void draw(Win *w,int x,int y,int cw,int ch) { (void)w;(void)x;(void)y;(void)cw;(void)ch; }
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"UI Tests"};
int kext_entry(const Kapi *k)
{
    api=k;
    static const AppDesc d={.title="UI Test Controller",.max_inst=1,.draw=draw,.client_size=size};
    if (k->register_app(&d)<0) return 1;
    return k->register_cmd("uitest","uitest check|finish|open APP",command);
}
