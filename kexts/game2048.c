#include "kapi.h"
#include "gdi.h"
#include "merge_core.inc"
#include "ui.inc"
static const Kapi *api;
static u8 board[16],undo[16];
static u32 score,prior,best,rng,prior_rng;
static int can_undo,won,started;
static u32 random_next(void){rng=rng*1664525u+1013904223u;return rng;}
static void spawn(void)
{
    int empty[16],n=0;for(int i=0;i<16;i++)if(!board[i])empty[n++]=i;
    if(n)board[empty[(random_next()>>16)%(u32)n]]=(random_next()>>16)%10==0?2:1;
}
static void fresh(void)
{
    for(int i=0;i<16;i++)board[i]=0;score=0;can_undo=won=0;spawn();spawn();started=1;
}
static void opened(int i){(void)i;if(!started){rng=*api->ticks^0x2048u;fresh();}}
static void move(int dir)
{
    u8 copy[16];u32 gain;for(int i=0;i<16;i++)copy[i]=board[i];
    if(!mg_move(board,dir,&gain))return;
    for(int i=0;i<16;i++)undo[i]=copy[i];prior=score;prior_rng=rng;can_undo=1;
    score=score>0xffffffffu-gain?0xffffffffu:score+gain;if(score>best)best=score;
    spawn();for(int i=0;i<16;i++)if(board[i]>=11)won=1;
}
static void revert(void){if(!can_undo)return;for(int i=0;i<16;i++)board[i]=undo[i];score=prior;rng=prior_rng;can_undo=0;won=0;for(int i=0;i<16;i++)if(board[i]>=11)won=1;}
static void size(int *w,int *h){*w=272;*h=332;}
static void initial(int i,int *w,int *h){(void)i;size(w,h);}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;char t[64];const GdiOps *gfx=gdi_bind(api,11);
    api->kfmt(t,sizeof t,"Score %u",score);api->draw_text_clip(x+8,y+7,t,C_NAVY,cw-106);
    api->kfmt(t,sizeof t,"Best  %u",best);api->draw_text_clip(x+8,y+27,t,C_GRAY,cw-106);
    ui_button(x,y,ui_r(cw-94,3,86,22),"New game",0,1);ui_button(x,y,ui_r(cw-94,27,86,22),"Undo",0,can_undo);
    int side=cw-16;if(side>ch-76)side=ch-76;side/=4;int gx=(cw-side*4)/2;
    static const u8 color[]={C_G0+5,C_WHITE,C_SILVER,C_YELLOW,C_OLIVE,C_GREEN,C_TEAL,C_BBLUE,C_NAVY,C_PURPLE,C_MAROON,C_RED};
    static const u32 rgb[]={GRGB(190,184,174),GRGB(239,234,218),GRGB(235,219,180),GRGB(239,175,94),GRGB(235,144,71),GRGB(222,111,65),GRGB(202,79,54),GRGB(217,183,75),GRGB(199,164,56),GRGB(179,143,42),GRGB(157,122,31),GRGB(130,97,24)};
    api->fill_rect(x+gx-2,y+52-2,side*4+4,side*4+4,C_SHAD);
    for(int i=0;i<16;i++){
        int bx=x+gx+(i%4)*side,by=y+52+(i/4)*side;u8 v=board[i];int c=v>11?11:v;
        if(gfx){gfx->set_dither(1);gfx->fill_gradient(bx+2,by+2,side-4,side-4,rgb[c],rgb[c],1);gfx->set_dither(0);}
        else api->fill_rect(bx+2,by+2,side-4,side-4,color[c]);
        if(v)api->hline(bx+3,by+3,side-6,C_WHITE);
        if(v){if(v>=20)api->kfmt(t,sizeof t,"%uM",1u<<(v-20));else api->kfmt(t,sizeof t,"%u",1u<<v);int scale=side>=60&&v<10?2:1;
            api->draw_text_scaled(bx+(side-3-(int)api->strlen(t)*8*scale)/2,by+(side-3-16*scale)/2,t,v>=4?C_WHITE:C_BLACK,scale,scale);}
    }
    ui_status(x,y,cw,ch,!mg_available(board)?"No moves. Undo or New game.":won?"2048! Keep going.":"Arrows / WASD   U: undo");
}
static void key(int i,int k)
{
    (void)i;if(k==K_LEFT||k=='a')move(0);else if(k==K_DOWN||k=='s')move(1);else if(k==K_RIGHT||k=='d')move(2);
    else if(k==K_UP||k=='w')move(3);else if(k=='u'||k==26)revert();else if(k=='n')fresh();
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;(void)ch;ui_pointer(x,y,ev);
    if(ui_click(ui_r(cw-94,3,86,22),x,y,ev))fresh();else if(ui_click(ui_r(cw-94,27,86,22),x,y,ev))revert();
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"2048"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);static const AppDesc d={.live_draw=APP_INDEPENDENT,.title="2048",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_GAMES,
        .open=opened,.draw=draw,.key=key,.mouse=mouse,.client_size=initial,.min_client=size};
    return k->register_app(&d)<0;
}
