/* Updates Breakout ball, paddle, and brick state. */
#pragma once

#define BO_W 288
#define BO_H 208
#define BO_PW 48
#define BO_PY 188
#define BO_R 3
typedef struct { int x,y,vx,vy,paddle,level,lives,left,score,over; u8 brick[40]; } Breakout;
static void bo_serve(Breakout *s)
{
    s->x=(s->paddle+BO_PW/2)*256;s->y=(BO_PY-BO_R-1)*256;
    s->vx=(s->level&1)?320:-320;s->vy=-(512+(s->level<20?s->level:20)*16);
}
static void bo_level(Breakout *s)
{
    s->left=40;s->paddle=(BO_W-BO_PW)/2;
    for(int i=0;i<40;i++)s->brick[i]=(u8)(1+(s->level>1&&i<8));
    bo_serve(s);
}
static void bo_new(Breakout *s){s->level=1;s->lives=3;s->score=s->over=0;bo_level(s);}
static void bo_paddle(Breakout *s,int p)
{s->paddle=p<0?0:p>BO_W-BO_PW?BO_W-BO_PW:p;}

static int bo_step(Breakout *s)
{
    if(s->over)return 0;
    for(int step=0;step<8;step++){
        int ox=s->x,oy=s->y;s->x+=s->vx/8;s->y+=s->vy/8;
        if(s->x<BO_R*256){s->x=BO_R*256;if(s->vx<0)s->vx=-s->vx;}
        if(s->x>(BO_W-BO_R)*256){s->x=(BO_W-BO_R)*256;if(s->vx>0)s->vx=-s->vx;}
        if(s->y<BO_R*256){s->y=BO_R*256;if(s->vy<0)s->vy=-s->vy;}
        if(s->vy>0&&oy<=(BO_PY-BO_R)*256&&s->y>=(BO_PY-BO_R)*256&&
           s->x>=(s->paddle-BO_R)*256&&s->x<=(s->paddle+BO_PW+BO_R)*256){
            s->y=(BO_PY-BO_R)*256;s->vy=-s->vy;
            s->vx=((s->x/256-s->paddle-BO_PW/2)*32);
            if(s->vx>-128&&s->vx<128)s->vx=s->vx<0?-128:128;
        }
        for(int i=0;i<40;i++)if(s->brick[i]){
            int l=(16+i%8*32)*256,r=l+30*256,t=(20+i/8*14)*256,b=t+12*256;
            if(s->x+BO_R*256<=l||s->x-BO_R*256>=r||s->y+BO_R*256<=t||s->y-BO_R*256>=b)continue;
            if(ox+BO_R*256<=l||ox-BO_R*256>=r)s->vx=-s->vx;
            else s->vy=-s->vy;
            s->x=ox;s->y=oy;if(s->score<=2147483637)s->score+=10;
            if(!--s->brick[i])s->left--;
            if(!s->left){s->level++;bo_level(s);return 2;}
            break;
        }
        if(s->y>(BO_H+BO_R)*256){if(!--s->lives)s->over=1;bo_serve(s);return 1;}
    }
    return 0;
}
