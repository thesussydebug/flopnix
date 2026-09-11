#include "kapi.h"
#include "gdi.h"
#include "base_core.inc"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
static char input[36],result[4][33];static TextField field;
static int active=2,valid=1;static u32 value;
static const int bases[]={16,10,2,8};
static const char *const labels[]={"Hex","Decimal","Binary","Octal"};
static void convert(void){valid=bc_parse(input,bases[active],&value);if(valid==1)for(int i=0;i<4;i++)bc_format(value,bases[i],result[i]);}
static void opened(int i){(void)i;active=1;af_set(&field,input,sizeof input,"0");field.all=1;convert();}
static void size(int *w,int *h){*w=440;*h=280;}
static void initial(int i,int *w,int *h){(void)i;size(w,h);}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;ui_header(x,y,cw,"Type a number in any base");
    for(int i=0;i<4;i++){
        int yy=36+i*38;api->draw_text(x+12,y+yy+4,labels[i],i==active?C_NAVY:C_GRAY);
        if(i==active)af_draw(&field,x+78,y+yy,cw-150,1);
        else {api->panel(x+78,y+yy,cw-150,24,1);api->fill_rect(x+80,y+yy+2,cw-154,20,C_WHITE);
            api->draw_text_clip(x+84,y+yy+4,valid==1?result[i]:"--",C_BLACK,cw-162);}
        ui_button(x,y,ui_r(cw-66,yy,54,24),"Copy",0,valid==1);
    }
    char t[80];
    if(valid==1){
        api->kfmt(t,sizeof t,"%u bytes",value);api->draw_text(x+12,y+199,t,C_NAVY);
        api->kfmt(t,sizeof t,"%u.%03u KiB   %u.%03u MiB",value>>10,bc_thousandths(value,10),value>>20,bc_thousandths(value,20));
        api->draw_text_clip(x+12,y+218,t,C_BLACK,cw-24);
        api->kfmt(t,sizeof t,"%u.%03u GiB  (1 KiB = 1024 bytes)",value>>30,bc_thousandths(value,30));
        api->draw_text_clip(x+12,y+236,t,C_GRAY,cw-24);
    }
    ui_status(x,y,cw,ch,valid==1?"Unsigned 32-bit. Tab switches base; Ctrl+A selects all.":valid==0?"Enter a number to see its other bases.":valid==-2?"Too large. The maximum is 4,294,967,295.":"That digit is not valid in the selected base.");
}
static void choose(int n)
{
    if(n==active)return;if(valid!=1){af_set(&field,input,sizeof input,"");}else af_set(&field,input,sizeof input,result[n]);
    active=n;field.all=1;convert();
}
static void key(int i,int k){(void)i;if(k=='\t'){choose((active+1)%4);return;}af_key(&field,k);convert();}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;(void)ch;if(ev!=EV_PRESS)return;
    for(int n=0;n<4;n++)if(y>=36+n*38&&y<60+n*38){
        if(x>=cw-66&&x<cw-12){if(valid==1)api->clip_set_text(result[n]);}
        else if(x>=12&&x<cw-72)choose(n);return;
    }
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Base Converter"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);static const AppDesc d={.title="Base Converter",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .open=opened,.draw=draw,.key=key,.mouse=mouse,.client_size=initial,.min_client=size};
    return k->register_app(&d)<0;
}
