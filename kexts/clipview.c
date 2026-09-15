#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "sbdrag.inc"
static const Kapi *api;
static int top,rows,total,preview_h;
static u32 sequence;
static SbDrag drag;
static u8 content[4097];
static ClipInfo current;
#define ROW 20
static int text_type(const char *type)
{
    return !api->strcmp(type,"text")||!api->strcmp(type,"file")||!api->strcmp(type,"file.cut");
}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;api->fill_rect(x,y,cw,ch,C_FACE);
    int n=api->clip_history(0,&current,content,4096);char text[96];
    if(n<0){n=0;api->strlcpy(text,"Clipboard empty",sizeof text);}
    else api->kfmt(text,sizeof text,"%s / %u %s",current.type,current.size,current.size==1?"byte":"bytes");
    ui_header(x,y,cw,text);content[n]=0;
    if(sequence!=api->clip_sequence()){sequence=api->clip_sequence();top=0;drag.active=0;}
    preview_h=ch-204;
    ui_group(x+10,y+40,cw-20,preview_h+24,"Current clipboard");
    int px=x+18,py=y+52,pw=cw-36,ph=preview_h;
    api->panel(px,py,pw,ph,1);api->fill_rect(px+2,py+2,pw-4,ph-4,C_WHITE);
    rows=(ph-10)/14;if(rows<1)rows=1;int cols=(pw-SB_W-12)/8;if(cols<1)cols=1;
    int textual=text_type(current.type),hexcols=(cols-7)/3;if(hexcols<1)hexcols=1;if(hexcols>16)hexcols=16;
    total=1;int col=0;
    for(int i=0;i<n;i++){if(textual&&content[i]==0)break;if(content[i]=='\r')continue;if(content[i]=='\n'||++col>=cols){total++;col=0;}}
    if(!textual)total=(n+hexcols-1)/hexcols;
    if(top>total-rows)top=total-rows;if(top<0)top=0;
    int line=0;col=0;
    for(int i=0;textual&&i<n;i++){
        u8 c=content[i];if(textual&&!c)break;if(c=='\r')continue;if(c=='\n'){line++;col=0;continue;}
        if(line>=top&&line<top+rows)api->draw_char(px+6+col*8,py+5+(line-top)*14,c<32?'.':c,C_BLACK);
        if(++col>=cols){line++;col=0;}
    }
    if(!textual)for(int r=0;r<rows&&top+r<total;r++){
        int offset=(top+r)*hexcols,at=5;api->kfmt(text,sizeof text,"%04x ",offset);
        for(int j=0;j<hexcols&&offset+j<n;j++){api->kfmt(text+at,sizeof text-at,"%02x ",content[offset+j]);at+=3;}
        api->draw_text_clip(px+6,py+5+r*14,text,C_BLACK,pw-SB_W-12);
    }
    if(!n)api->draw_text(px+8,py+7,"Nothing copied yet.",C_GRAY);
    if(total>rows)api->draw_sbar(px+pw-SB_W-2,py+2,ph-4,0,total,rows,top);
    int hy=y+ch-110;
    ui_group(x+10,y+ch-122,cw-20,116,"History - click to restore (1-5)");
    api->panel(x+18,hy,cw-36,5*ROW+2,1);
    int found=0;
    for(int i=1;i<=5;i++){
        ClipInfo info;u8 preview[64];int got=api->clip_history(i,&info,preview,63);if(got<0)break;found++;
        preview[got]=0;if(text_type(info.type))for(int j=0;j<got;j++){if(!preview[j])break;if(preview[j]<32||preview[j]==127)preview[j]=' ';}
        int yy=hy+1+(i-1)*ROW;
        api->kfmt(text,sizeof text,"%d",i);api->draw_text(x+26,yy+3,text,C_NAVY);
        if(text_type(info.type))api->draw_text_clip(x+46,yy+3,(char *)preview,C_BLACK,cw-192);
        else {
            int at=0;
            for(int j=0;j<got&&j<16;j++){api->kfmt(text+at,sizeof text-at,"%02x ",preview[j]);at+=3;}
            if(!got)text[0]=0;api->draw_text_clip(x+46,yy+3,text,C_BLACK,cw-192);
        }
        api->draw_text_clip(x+cw-136,yy+3,info.type,C_GRAY,64);
        api->human_size(info.size,text,sizeof text);api->draw_text_clip(x+cw-68,yy+3,text,C_GRAY,44);
    }
    if(!found)api->draw_text(x+26,hy+7,"No previous entries.",C_GRAY);
}
static void mouse(int inst,int x,int y,int ev,int cw,int ch)
{
    (void)inst;
    if(ev==EV_RELEASE){drag.active=0;return;}
    if(ev==EV_DRAG){if(drag.active)top=sb_move(&drag,preview_h-4,total,rows,y-54);return;}
    if(ev!=EV_PRESS)return;
    if(x>=cw-20-SB_W&&x<cw-20&&y>=54&&y<52+preview_h-2&&total>rows){top=sb_press(&drag,preview_h-4,total,rows,top,y-54);return;}
    int hy=ch-110;
    if(x>=20&&x<cw-20&&y>=hy+1&&y<hy+1+5*ROW){if(api->clip_restore(1+(y-hy-1)/ROW)==0){top=0;drag.active=0;}}
}
static void wheel(int i,int dz){(void)i;top-=dz*3;if(top<0)top=0;}
static void key(int i,int k){(void)i;if(k>='1'&&k<='5')api->clip_restore(k-'0');else if(k==K_UP)top--;else if(k==K_DOWN)top++;if(top<0)top=0;}
static void size(int i,int *w,int *h){(void)i;*w=484;*h=342;}
static void minimum(int *w,int *h){*w=416;*h=282;}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"Clipboard"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;api=k;ui_init(k,0);
    static const AppDesc d={.live_draw=APP_INDEPENDENT,.title="Clipboard Viewer",.max_inst=1,.in_menu=1,.resizable=1,
        .category=APP_CAT_SYSTEM,.draw=draw,.mouse=mouse,.wheel=wheel,.key=key,.client_size=size,.min_client=minimum};
    return k->register_app(&d)<0;
}
