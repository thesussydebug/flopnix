#include "kapi.h"
#include "gdi.h"
#include "breakout_core.inc"
#include "ui.inc"
static const Kapi *api;
static Breakout game;
static int type=-1,timer=-1,running,started,ready=1;
static void pause_game(void){running=0;if(timer>=0){api->timer_del(timer);timer=-1;}}
static void tick(void *ctx)
{
    (void)ctx;int focused=0;
    for(int i=0;i<api->win_max();i++){const Win *w=api->win_slot(i);if(w&&w->type==type&&api->win_is_focused((Win *)w)){focused=1;break;}}
    if(!focused){pause_game();api->gui_dirty();return;}
    int dir=(api->key_down(K_RIGHT)||api->key_down('d'))-(api->key_down(K_LEFT)||api->key_down('a'));
    bo_paddle(&game,game.paddle+dir*5);
    if(bo_step(&game)){ready=1;pause_game();}
    api->gui_dirty();
}
static void toggle(void)
{
    if(running){pause_game();return;}
    if(game.over)return;
    timer=api->timer_add(2,tick,0);running=timer>=0;if(running)ready=0;
}
static void opened(int i){(void)i;if(!started){bo_new(&game);started=1;}}
static void closed(int i){(void)i;pause_game();}
static void key(int i,int k)
{
    (void)i;if(k==' '||k=='p')toggle();else if(k=='n'){pause_game();bo_new(&game);ready=1;}
    else if(!running&&(k==K_LEFT||k==K_RIGHT||k=='a'||k=='d')){bo_paddle(&game,game.paddle+(k==K_LEFT||k=='a'?-8:8));if(ready)bo_serve(&game);}
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;(void)cw;(void)ch;if(ev!=EV_PRESS&&ev!=EV_DRAG)return;
    if(y>=26&&y<26+BO_H){bo_paddle(&game,x-BO_PW/2);if(ready)bo_serve(&game);}
    else if(ev==EV_PRESS&&y>=26+BO_H){if(x<90){pause_game();bo_new(&game);ready=1;}else toggle();}
}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;(void)ch;char text[64];
    api->kfmt(text,sizeof text,"%d points   Level %d   Lives %d",game.score,game.level,game.lives);ui_header(x,y,cw,text);
    int fy=y+26;api->fill_rect(x,fy,BO_W,BO_H,C_NAVY);
    static const u8 colors[]={C_RED,C_YELLOW,C_BGREEN,C_CYAN,C_PURPLE};
    for(int i=0;i<40;i++)if(game.brick[i]){
        int bx=x+16+i%8*32,by=fy+20+i/8*14;api->fill_rect(bx,by,30,12,colors[i/8]);api->bevel(bx,by,30,12,0);
        if(game.brick[i]>1)api->hline(bx+8,by+5,14,C_WHITE);
    }
    api->fill_rect(x+game.paddle,fy+BO_PY,BO_PW,6,C_SILVER);api->bevel(x+game.paddle,fy+BO_PY,BO_PW,6,0);
    api->fill_rect(x+game.x/256-BO_R,fy+game.y/256-BO_R,6,6,C_WHITE);
    if(!running){const char *s=game.over?"Game over - New to play again":"Space to play / pause";api->draw_text(x+(cw-(int)api->strlen(s)*8)/2,fy+112,s,C_WHITE);}
    ui_button(x,y,ui_r(4,BO_H+29,80,22),"New",0,1);
    ui_button(x,y,ui_r(90,BO_H+29,80,22),running?"Pause":"Play",0,!game.over);
    api->draw_text(x+182,y+BO_H+33,"Arrows / A D",C_GRAY);
}
static void size(int i,int *w,int *h){(void)i;*w=BO_W;*h=BO_H+55;}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Breakout"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);static const AppDesc d={.title="Breakout",.max_inst=1,.in_menu=1,.category=APP_CAT_GAMES,
        .open=opened,.close=closed,.draw=draw,.key=key,.mouse=mouse,.client_size=size};
    type=k->register_app(&d);return type<0;
}
