#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "sbdrag.inc"
#include "kextstate.h"
static const Kapi *api;
static int selected=-1,top,visible;
static SbDrag drag;
#define LIST 56
#define ROW 16
static void clamp(int ch)
{
    visible=(ch-LIST-48)/ROW;if(visible<1)visible=1;
    int last=api->kext_count()-visible;if(last<0)last=0;
    if(top>last)top=last;if(top<0)top=0;
}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;clamp(ch);api->fill_rect(x,y,cw,ch,C_FACE);
    int count=api->kext_count(),loaded=0;u32 code=0;
    for(int i=0;i<count;i++){const KextInfo *k=api->kext_get(i);if(k){code+=k->size;if(!k->status)loaded++;}}
    char s[96];api->kfmt(s,sizeof s,"%d loaded / %d extensions",loaded,count);
    ui_header(x,y,cw,s);
    api->kfmt(s,sizeof s,"Code: %u KB",(code+1023)/1024);ui_header_right(x,y,cw,s,C_NAVY);
    int type=cw-264,memory=cw-208,state=cw-144;
    int edges[]={8,type-6,memory-6,state-6,cw-8};const char *labels[]={"Extension","Kind","Code","State"};
    for(int i=0;i<4;i++){api->panel(x+edges[i],y+32,edges[i+1]-edges[i],23,0);api->draw_text(x+edges[i]+6,y+36,labels[i],C_BLACK);}
    api->panel(x+8,y+LIST-1,cw-16,visible*ROW+2,1);
    for(int r=0;r<visible&&top+r<count;r++){
        int i=top+r,yy=y+LIST+r*ROW;const KextInfo *k=api->kext_get(i);if(!k)continue;
        int on=i==selected;u8 fg=on?C_WHITE:kx_failed(k->status)?C_MAROON:C_BLACK;
        if(on)api->fill_rect(x+10,yy,cw-20-SB_W,ROW,C_HILITE);
        api->draw_text_clip(x+14,yy+1,k->hname[0]?k->hname:k->name,fg,type-22);
        api->draw_text(x+type,yy+1,k->kind==KEXT_KIND_APP?"App":"Core",fg);
        api->kfmt(s,sizeof s,"%uK",(k->size+1023)/1024);api->draw_text(x+memory,yy+1,s,fg);
        api->draw_text_clip(x+state,yy+1,kx_state(k->status),fg,128-SB_W);
    }
    if(count>visible)api->draw_sbar(x+cw-8-SB_W,y+LIST,visible*ROW,0,count,visible,top);
    int dy=y+ch-40;api->panel(x+8,dy,cw-16,34,1);
    const KextInfo *k=selected>=0?api->kext_get(selected):0;
    if(k){
        api->draw_text_clip(x+14,dy+3,k->name,C_NAVY,cw-28);
        if(k->size)api->kfmt(s,sizeof s,"%08x  /  %u bytes of code",k->base,k->size);
        else api->strlcpy(s,kx_state(k->status),sizeof s);
        api->draw_text_clip(x+14,dy+18,s,C_BLACK,cw-28);
    }else api->draw_text(x+14,dy+10,"Select an extension for its path and address.",C_GRAY);
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;clamp(ch);
    if(ev==EV_RELEASE){drag.active=0;return;}
    if(ev==EV_DRAG){if(drag.active)top=sb_move(&drag,visible*ROW,api->kext_count(),visible,y-LIST);return;}
    if(ev!=EV_PRESS||y<LIST||y>=LIST+visible*ROW)return;
    if(x>=cw-8-SB_W&&x<cw-8&&api->kext_count()>visible)top=sb_press(&drag,visible*ROW,api->kext_count(),visible,top,y-LIST);
    else if(x>=8&&x<cw-8-SB_W){int n=top+(y-LIST)/ROW;if(n<api->kext_count())selected=n;}
}
static void wheel(int i,int dz){(void)i;top-=dz*3;if(top<0)top=0;}
static void key(int i,int k)
{
    (void)i;int n=api->kext_count();if(!n)return;
    if(k==K_DOWN&&selected<n-1)selected++;else if(k==K_UP&&selected>0)selected--;else return;
    if(selected<top)top=selected;if(selected>=top+visible)top=selected-visible+1;
}
static void size(int i,int *w,int *h){(void)i;*w=520;*h=300;}
static void minimum(int *w,int *h){*w=468;*h=222;}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"KEXT Inspector"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;api=k;ui_init(k,0);
    static const AppDesc d={.live_draw=APP_INDEPENDENT,.title="KEXT Inspector",.max_inst=1,.in_menu=1,.resizable=1,
        .draw=draw,.mouse=mouse,.key=key,.wheel=wheel,.client_size=size,.min_client=minimum,.category=APP_CAT_DEV};
    return k->register_app(&d)<0;
}
