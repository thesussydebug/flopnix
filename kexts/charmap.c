#include "kapi.h"
#include "gdi.h"
#include "charmap_core.inc"
#include "ui.inc"
static const Kapi *api;
static int selected=65,copied;
static void cm_size(int *w,int *h){*w=360;*h=400;}
static void cm_initial(int i,int *w,int *h){(void)i;cm_size(w,h);}
static void cm_copy(void)
{
    u8 b=(u8)selected;
    copied=api->clip_set("text",&b,1)==0?1:-1;
}
static void cm_draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;ui_header(x,y,cw,"Select a character to copy");
    int sw=(cw-36)/16,sh=(ch-80)/16,gx=(cw-sw*16)/2,gy=40;
    char t[90];
    for(int i=0;i<16;i++){char n="0123456789ABCDEF"[i];api->draw_char(x+gx+i*sw+(sw-8)/2,y+26,n,C_GRAY);}
    for(int i=0;i<256;i++){
        int bx=x+gx+(i%16)*sw,by=y+gy+(i/16)*sh;
        api->fill_rect(bx,by,sw-1,sh-1,i==selected?C_NAVY:C_WHITE);
        api->draw_char(bx+(sw-8)/2,by+(sh-16)/2,(char)i,i==selected?C_WHITE:C_BLACK);
    }
    api->kfmt(t,sizeof t,"Code %u  /  0x%02x   %s",selected,selected,
        copied<0?"Clipboard unavailable":copied?"Copied":"Arrows to select; Enter copies");
    api->draw_text_clip(x+12,y+ch-37,t,C_NAVY,cw-24);
    ui_status(x,y,cw,ch,selected<32||selected==127?"Control byte: text apps may treat this specially.":"Paste into Notes or Editor with Ctrl+V.");
}
static void cm_mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;if(ev!=EV_PRESS)return;
    int sw=(cw-36)/16,sh=(ch-80)/16;
    int n=cm_hit(x,y,(cw-sw*16)/2,40,sw,sh);if(n>=0){selected=n;cm_copy();}
}
static void cm_key(int i,int k)
{
    (void)i;copied=0;
    if(k==K_LEFT&&selected>0)selected--;else if(k==K_RIGHT&&selected<255)selected++;
    else if(k==K_UP&&selected>=16)selected-=16;else if(k==K_DOWN&&selected<240)selected+=16;
    else if(k==K_HOME)selected=0;else if(k==K_END)selected=255;else if(k=='\n'||k==3)cm_copy();
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Character Map"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);
    static const AppDesc d={.title="Character Map",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .draw=cm_draw,.key=cm_key,.mouse=cm_mouse,.client_size=cm_initial,.min_client=cm_size};
    return k->register_app(&d)<0;
}
