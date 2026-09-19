#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "sbdrag.inc"
#include "kextstate.h"
static const Kapi *api;
static int tab,top,visible,count,list;
static SbDrag drag;
static MemBuffer active[32];
static int parts[32];
#define LIST list
#define ROW 24
static const struct {int lo,hi;const char *name;} regions[]={
    {MI_KERNEL_BASE,MI_KERNEL_END,"Kernel"},
    {MI_DMA_BASE,MI_DMA_END,"Floppy DMA"},
    {MI_FB_BASE,MI_FB_END,"Screen buffer"},
    {MI_IO_BASE,MI_IO_END,"File transfer buffer"},
    {MI_ARENA_BASE,MI_ARENA_END,"Extensions / core data"},
    {MI_POOL_BASE,MI_POOL_END,"App data pages"},
    {MI_HEAP_BASE,MI_HEAP_END,"Initial heap"},
    {MI_HEAP_GROW_BASE,MI_HEAP_GROW_END,"Heap growth"}
};
static const u8 colors[]={C_BBLUE,C_OLIVE,C_MAROON,C_TEAL,C_PURPLE,C_TEAL,C_GREEN,C_GREEN};
static const u32 shades[][2]={
    {GRGB(64,128,255),GRGB(0,48,224)},{GRGB(255,192,0),GRGB(224,112,0)},
    {GRGB(255,64,64),GRGB(192,0,0)},{GRGB(0,224,224),GRGB(0,128,192)},
    {GRGB(224,64,255),GRGB(144,0,192)},{GRGB(0,224,192),GRGB(0,128,128)},
    {GRGB(64,224,64),GRGB(0,144,0)},{GRGB(64,224,64),GRGB(0,144,0)}
};
static int scale(u32 n,u32 cap,int width)
{
    if(!cap||width<1)return 0;if(n>cap)n=cap;
    while(cap>65535){cap>>=1;n>>=1;}
    return (int)(n*(u32)width/cap);
}
static u32 capacity(int i)
{
    u32 base=api->mem_info(regions[i].lo),end=api->mem_info(regions[i].hi);
    return i==0?api->mem_info(MI_KERNEL_BYTES):end>base?end-base:0;
}
static u32 usage(int i)
{
    u32 cap=i==6?api->mem_info(MI_HEAP_CAPACITY):capacity(i),n=cap;
    if(i==4)n=api->mem_info(MI_ARENA_RO)+api->mem_info(MI_ARENA_RW);
    else if(i==5)n=api->mem_info(MI_POOL_USED);
    else if(i==6){u32 free=api->mem_info(MI_HEAP_FREE);n=free<cap?cap-free:0;}
    return n>cap?cap:n;
}
static void shade(int x,int y,int w,int h,int i)
{
    if(w<1||h<1)return;
    if(ui_gfx){ui_gfx->set_dither(1);ui_gfx->fill_gradient(x,y,w,h,shades[i][0],shades[i][1],1);ui_gfx->set_dither(0);}
    else api->fill_rect(x,y,w,h,colors[i]);
}
static void bar(int x,int y,int w,int h,u32 n,u32 cap,int i)
{
    api->panel(x,y,w,h,1);api->fill_rect(x+2,y+2,w-4,h-4,C_G0+6);
    shade(x+2,y+2,scale(n,cap,w-4),h-4,i);
}
static void overview(int x,int y,int cw)
{
    char s[96],a[24],b[24],c[24];u32 ram=api->mem_total_kb()*1024,allocated=0,used=api->mem_used_kb()*1024;
    for(int i=0;i<8;i++)allocated+=capacity(i);
    ui_group(x+10,y+78,cw-20,76,"Physical memory");
    api->human_size(allocated,a,sizeof a);api->human_size(used,b,sizeof b);api->human_size(ram,c,sizeof c);
    api->kfmt(s,sizeof s,"%s allocated (%s used) / %s Memory",a,b,c);api->draw_text_clip(x+22,y+90,s,C_BLACK,cw-44);
    api->panel(x+22,y+112,cw-44,18,1);api->fill_rect(x+24,y+114,cw-48,14,C_G0+6);
    u32 groups[]={capacity(0)+capacity(1)+capacity(2)+capacity(3),capacity(4)+capacity(5),capacity(6)+capacity(7)};
    const int ids[]={0,4,6};const char *legend[]={"System","Extensions","Heap","Unmapped"};u32 cumulative=0;int prev=0;
    for(int i=0;i<3;i++){cumulative+=groups[i];int next=scale(cumulative,ram,cw-48);shade(x+24+prev,y+114,next-prev,14,ids[i]);prev=next;}
    for(int i=0;i<4;i++){int xx=x+22+i*(cw-44)/4;if(i<3)shade(xx,y+136,8,8,ids[i]);else api->fill_rect(xx,y+136,8,8,C_G0+6);api->draw_text(xx+12,y+133,legend[i],C_BLACK);}
    const char *names[]={"Extensions","App pages","Heap"};int gw=(cw-28)/3;
    for(int i=0;i<3;i++){
        int xx=x+10+i*(gw+4);u32 cap=i==2?api->mem_info(MI_HEAP_CAPACITY):capacity(i+4),n=usage(i+4);
        ui_group(xx,y+161,gw,73,names[i]);api->human_size(n,a,sizeof a);api->human_size(cap,b,sizeof b);
        api->kfmt(s,sizeof s,"%s / %s",a,b);api->draw_text_clip(xx+9,y+176,s,C_BLACK,gw-18);
        bar(xx+9,y+199,gw-18,20,n,cap,i+4);api->kfmt(s,sizeof s,"%d%%",scale(n,cap,100));
        int tx=xx+(gw-api->text_width(s))/2,split=xx+11+scale(n,cap,gw-22);
        api->draw_text_clip2(tx,y+202,s,C_WHITE,xx+11,split);api->draw_text_clip2(tx,y+202,s,C_BLACK,split,xx+gw-11);
    }
}
static void totals(int ch)
{
    list=tab?110:266;
    visible=(ch-LIST-10)/ROW;if(visible<1)visible=1;
    if(tab==2)count=sizeof regions/sizeof regions[0];
    else if(tab==1)count=api->kext_count();
    else {
        MemBuffer b;count=0;
        for(int i=0;api->mem_buffer(i,&b);i++){
            int slot=0;while(slot<count&&api->strcmp(active[slot].name,b.name))slot++;
            if(slot<count){active[slot].size+=b.size;active[slot].base=0;parts[slot]++;}
            else if(count<32){active[count]=b;parts[count++]=1;}
        }
        for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)if(active[j].size>active[i].size){MemBuffer b=active[i];active[i]=active[j];active[j]=b;int n=parts[i];parts[i]=parts[j];parts[j]=n;}
    }
    int last=count-visible;if(last<0)last=0;if(top>last)top=last;if(top<0)top=0;
}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;totals(ch);api->fill_rect(x,y,cw,ch,C_FACE);
    char s[96],a[24];api->human_size(api->mem_info(MI_HEAP_FREE),a,sizeof a);
    api->kfmt(s,sizeof s,"Heap free: %s",a);ui_header(x,y,cw,s);
    api->human_size(api->mem_info(MI_HEAP_LARGEST),a,sizeof a);api->kfmt(s,sizeof s,"Largest block: %s",a);ui_header_right(x,y,cw,s,C_NAVY);
    const char *tabs[3]={"Overview","Components","Regions"};
    for(int i=0;i<3;i++)ui_button(x,y,ui_r(10+i*124,34,120,26),tabs[i],tab==i,1);
    u32 max=api->mem_total_kb()*1024;
    if(!tab){overview(x,y,cw);max=count?active[0].size:0;}
    else{
        max=0;for(int i=0;i<count;i++){u32 n=0;if(tab==1){const KextInfo *k=api->kext_get(i);if(k)n=k->size;}else n=capacity(i);if(n>max)max=n;}
        api->draw_text(x+16,y+74,tab==1?"Loaded extensions":"Allocated memory regions",C_NAVY);
    }
    int address=cw-276,bytes=cw-188,bx=cw-108;
    api->panel(x+8,y+LIST-22,cw-16,22,0);api->draw_text(x+16,y+LIST-18,tab?"Name":"Active buffer",C_BLACK);
    api->draw_text(x+address,y+LIST-18,"Address",C_BLACK);api->draw_text(x+bytes,y+LIST-18,"Size",C_BLACK);
    api->draw_text(x+bx,y+LIST-18,"Relative",C_BLACK);
    api->panel(x+8,y+LIST-1,cw-16,visible*ROW+2,1);
    for(int r=0;r<visible&&top+r<count;r++){
        int i=top+r,yy=y+LIST+r*ROW;u32 base=0,size=0;const char *name="";u8 fg=C_BLACK;MemBuffer buf;
        if(tab==2){base=api->mem_info(regions[i].lo);size=capacity(i);name=regions[i].name;}
        else if(tab==1){const KextInfo *k=api->kext_get(i);if(!k)continue;base=k->base;size=k->size;name=k->hname[0]?k->hname:k->name;if(kx_failed(k->status))fg=C_MAROON;}
        else{buf=active[i];base=buf.base;size=buf.size;name=buf.name;}
        u32 hash=0;for(const char *p=name;*p;p++)hash=hash*33+(u8)*p;int color=tab==2?i:(int)(hash%7);
        shade(x+16,yy+8,8,8,color);
        if(!tab&&parts[i]>1){api->kfmt(s,sizeof s,"%s (%d)",name,parts[i]);name=s;}
        api->draw_text_clip(x+30,yy+5,name,fg,address-36);
        if(base)api->kfmt(s,sizeof s,"%08x",base);else api->strlcpy(s,!tab&&parts[i]>1?"Multiple":"-",sizeof s);
        api->draw_text(x+address,yy+5,s,C_GRAY);
        api->human_size(size,s,sizeof s);api->draw_text_clip(x+bytes,yy+5,s,C_BLACK,bx-bytes-6);
        bar(x+bx,yy+5,80,14,size,max,color);
    }
    if(!count)api->draw_text(x+14,y+LIST+8,"No active buffers.",C_GRAY);
    if(count>visible)api->draw_sbar(x+cw-8-SB_W,y+LIST,visible*ROW,0,count,visible,top);
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;totals(ch);
    if(ev==EV_RELEASE){drag.active=0;return;}
    if(ev==EV_DRAG){if(drag.active)top=sb_move(&drag,visible*ROW,count,visible,y-LIST);return;}
    if(ev!=EV_PRESS)return;
    if(x>=10&&x<382&&y>=34&&y<60&&(x-10)%124<120){tab=(x-10)/124;top=0;drag.active=0;return;}
    if(x>=cw-8-SB_W&&x<cw-8&&y>=LIST&&y<LIST+visible*ROW&&count>visible)
        top=sb_press(&drag,visible*ROW,count,visible,top,y-LIST);
}
static void wheel(int i,int dz){(void)i;top-=dz*3;if(top<0)top=0;}
static void key(int i,int k){(void)i;if(k=='\t'){tab=(tab+1)%3;top=0;}else if(k==K_UP)top--;else if(k==K_DOWN)top++;if(top<0)top=0;}
static void size(int i,int *w,int *h){(void)i;*w=560;*h=396;}
static void minimum(int *w,int *h){*w=516;*h=326;}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"Memory Map"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;api=k;ui_init(k,0);
    static const AppDesc d={.live_draw=APP_INDEPENDENT,.title="Memory Map",.max_inst=1,.in_menu=1,.resizable=1,.draw=draw,
        .mouse=mouse,.wheel=wheel,.key=key,.client_size=size,.min_client=minimum,.category=APP_CAT_SYSTEM};
    return k->register_app(&d)<0;
}
