#include "kapi.h"
#include "gdi.h"
#include "g3d.h"
static const Kapi *api;
static int failures;
static int close_fault;
static u8 *probe_buffer;
static void probe_open(int i){(void)i;probe_buffer=api->kmalloc(4096);__asm__ volatile("ud2");}
static void probe_close(int i){(void)i;api->kfree(probe_buffer);probe_buffer=0;if(close_fault)__asm__ volatile("ud2");}
static void probe_draw(Win *w,int x,int y,int cw,int ch){(void)w;(void)x;(void)y;(void)cw;(void)ch;}
static void probe_size(int i,int *w,int *h){(void)i;*w=*h=64;}
static void serial(const char *s){for(;*s;s++){for(int i=0;i<100000;i++)if(api->inb(0x3fd)&32)break;api->outb(0x3f8,(u8)*s);}}
static void check(int ok,const char *name){serial(ok?"LIFECYCLE PASS ":"LIFECYCLE FAIL ");serial(name);serial("\n");if(!ok)failures++;}
static void run(const char *args){
    (void)args;failures=0;
    if(!api->strcmp(args,"open-fault")){
        static const AppDesc d={.title="Failure Probe",.max_inst=1,.open=probe_open,.close=probe_close,.draw=probe_draw,.client_size=probe_size};
        int type=api->register_app(&d);check(type>=0,"failure probe registered");u32 before=api->heap_avail();
        if(type>=0)for(int i=0;i<2;i++){
            close_fault=i;
            check(api->win_open(type)<0,"failed open refused");check(!probe_buffer&&api->heap_avail()==before,"failed open releases memory");
            if(probe_buffer)probe_close(0);
        }
        close_fault=0;
        char b[64];api->kfmt(b,sizeof b,"LIFECYCLE DONE: %d failures\n",failures);serial(b);return;
    }
    if(!api->strcmp(args,"graphics")){
        const G3dOps *g3=api->service_get("g3d");check(g3!=0,"G3D available");
        if(g3){
            u32 original=api->heap_avail();G3D *c=g3->create();check(c!=0,"G3D context");
            if(c){u32 before=api->heap_avail();g3->viewport(c,0,0,65537,65537,FX(1),FX(9));g3->clearz(c,FX(1));
                check(api->heap_avail()==before,"overflowed viewport refused");
                g3->viewport(c,0,0,64,64,FX(1),FX(9));g3->clearz(c,FX(1));check(api->heap_avail()<before,"normal depth buffer works");
                g3->destroy(c);check(api->heap_avail()==original,"depth buffer released");}
        }
        char b[64];api->kfmt(b,sizeof b,"LIFECYCLE DONE: %d failures\n",failures);serial(b);return;
    }
    static const char *const titles[]={"Pong","Snake","Tetris","Fault Log","Serial Monitor"};
    static const char *const paths[]={"sys/pong.kx","sys/snake.kx","sys/tetris.kx","sys/faultlog.kx","sys/serialmon.kx"};
    for(int n=0;n<5;n++){
        int type=api->app_find(titles[n]);int ok=type>=0;
        for(int i=0;i<12&&ok;i++){int inst=api->win_open(type);if(inst<0){ok=0;break;}api->win_close_self(type,inst);}
        check(ok,titles[n]);int owner=-1;
        for(int i=0;i<api->kext_count();i++){const KextInfo *k=api->kext_get(i);if(k&&!api->strcmp(k->name,paths[n]))owner=i;}
        check(owner>=0&&api->kext_unload(owner)==0,paths[n]);
        if(owner>=0)check(!api->kext_load(paths[n]),"reload after close");
    }
    check(!api->fault_count(),"no recovered faults");char b[80];api->kfmt(b,sizeof b,"LIFECYCLE DONE: %d failures; %u free bytes\n",failures,api->heap_avail());serial(b);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Lifecycle Tests"};
int kext_entry(const Kapi *k){api=k;k->register_cmd("lifecycle","lifecycle - app cleanup checks",run);return 0;}
