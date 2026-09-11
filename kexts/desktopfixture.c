/* A sample payload for desktop tests. */
#include "kapi.h"
static const Kapi *api;
static int started,tid=-1;
static void tick(void *c) { (void)c; }
static void hold(void *c)
{
    (void)c; api->timer_del(tid); tid=-1;
    const char *s="UITEST HOLD START\n";
    while (*s) { while (!(api->inb(0x3fd)&32)) {} api->outb(0x3f8,*s++); }
    volatile u8 *flag=api->iobuf; flag[0]=1;
    u32 end=*api->ticks+800;
    while (flag[0]!=2 && (i32)(*api->ticks-end)<0) {}
    s="UITEST HOLD END\n";
    while (*s) { while (!(api->inb(0x3fd)&32)) {} api->outb(0x3f8,*s++); }
}
static int hold_open(const char *n,const char *p,const u8 *d,int len)
{
    (void)n;(void)p;(void)d;(void)len;
    ((volatile u8 *)api->iobuf)[0]=0;
    tid=api->timer_add(10,hold,0); return tid<0 ? -1 : 0;
}
static void size(int i,int *w,int *h) { (void)i; *w=220; *h=80; }
static void draw(Win *w,int x,int y,int cw,int ch)
{ (void)w; (void)cw; (void)ch; api->draw_text(x+8,y+8,"Desktop module opened",C_BLACK); }
static void starts(const char *args) {
    if (!api->strcmp(args,"hold")) tid=api->timer_add(600,hold,0);
    else if (!api->strcmp(args,"timeron")) tid=api->timer_add(10,tick,0);
    else if (!api->strcmp(args,"timeroff")) { api->timer_del(tid); tid=-1; }
    else api->kfmt((char *)api->iobuf,40,"entry calls: %d",started);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Desktop Probe"};
int kext_entry(const Kapi *k)
{
    api=k; started++;
    static const AppDesc d={.title="Desktop Probe",.max_inst=1,.draw=draw,.client_size=size};
    api->register_cmd("fixture", "fixture", starts);
    api->register_opener("hold",hold_open);
    return api->register_app(&d)<0;
}
