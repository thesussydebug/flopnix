#include "kapi.h"
#include "gdi.h"
#include "nettext.h"
#include "textweb_core.inc"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
static TwPage page;
static char address[TW_URL],current[TW_URL],search_url[TW_URL],status[100];
static TextField field;
typedef struct {char url[TW_URL];int scroll;} History;
static History history[8];static int hcount,hpos=-1,top,links_view,selected,focus=1,searching,started,at_home=1,view_rows=10;
static int back_target(void){return at_home?hpos:hpos-1;}
typedef struct {u8 *data;int len,full;} Download;
static void say(const char *s){api->strlcpy(status,s,sizeof status);api->gui_dirty();}
static void copy_text(void)
{
    const char *s=links_view&&page.links?page.link[selected].url:page.text;
    u32 n=api->strlen(s);if(n>4095)n=4095;
    if(api->clip_set("text",s,n)!=0)say("The clipboard is unavailable.");
    else say(api->strlen(s)>n?"Copied the first 4095 characters (clipboard limit).":"Copied to the clipboard.");
}
static int receive(const u8 *s,int n,void *ctx)
{
    Download *d=ctx;
    if(n<0||n>65536-d->len){d->full=1;return 0;}
    api->memcpy(d->data+d->len,s,(u32)n);d->len+=n;d->data[d->len]=0;return 1;
}
static void set_address(const char *s){af_set(&field,address,sizeof address,s);}
static void navigate(const char *url,int target)
{
    TwUrl u;char requested[TW_URL];
    if(!tw_url(url,&u)){say("Use gopher:// or http:// with a valid address. HTTPS is unavailable.");return;}
    api->strlcpy(requested,url,sizeof requested);
    if(!u.http&&u.type=='7'&&!tw_contains(u.path,"\t")){
        api->strlcpy(search_url,requested,sizeof search_url);set_address("");searching=focus=1;say("Type your search words, then press Enter.");return;
    }
    if(!api->net_up()||!api->net_get(NET_IP)){say("Network is not ready. Check the Network page in Settings.");return;}
    const NetTextOps *net=(const NetTextOps *)api->service_get("net.text");
    if(!u.http&&(!net||net->abi!=NET_TEXT_ABI||!net->request)){say("Gopher needs the updated net.kx extension.");return;}
    Download d={api->kmalloc(65537),0,0};TwPage *next=api->kmalloc(sizeof *next);
    if(!d.data||!next){if(d.data)api->kfree(d.data);if(next)api->kfree(next);say("Not enough memory. Close another app and try again.");return;}
    d.data[0]=0;say("Connecting... Press Esc to stop.");api->esc_arm();
    u32 ip=api->net_dns(u.host,400);int result=-3;
    if(ip&&!api->esc_pending()){
        say("Receiving text... Press Esc to stop.");
        if(u.http){
            char host[90];if(u.port==80)api->strlcpy(host,u.host,sizeof host);else api->kfmt(host,sizeof host,"%s:%u",u.host,u.port);
            result=api->net_http_get(ip,u.port,host,u.path,receive,&d,800);
        }else{
            char req[180];api->kfmt(req,sizeof req,"%s\r\n",u.path);result=net->request(ip,u.port,req,receive,&d,800);
        }
    }
    if(result==(u.http?200:0)){

        int mode=u.http?0:u.type=='1'||u.type=='7'?2:u.type=='h'?1:3;
        if(u.http){int p=0;while(d.data[p]==' '||d.data[p]=='\n'||d.data[p]=='\r'||d.data[p]=='\t')p++;if(d.data[p]=='<')mode=1;}
        tw_render(next,(const char *)d.data,mode,requested);
        if(hpos>=0&&!at_home)history[hpos].scroll=top;
        if(target>=0){hpos=target;top=history[hpos].scroll;}
        else{
            hcount=hpos+1;
            if(hcount==8){api->memmove(history,history+1,7*sizeof history[0]);hcount--;}
            hpos=hcount++;api->strlcpy(history[hpos].url,requested,TW_URL);history[hpos].scroll=0;top=0;
        }

        api->memcpy(&page,next,sizeof page);set_address(requested);api->strlcpy(current,requested,sizeof current);
        links_view=selected=focus=searching=at_home=0;
        api->kfmt(status,sizeof status,"%u bytes, %d links.%s",(u32)d.len,page.links,page.clipped?" Display limit reached; showing part of this page.":" Select Links to follow a link.");
    }else if(d.full)say("Page exceeds 64 KiB. The previous page has been kept.");
    else if(result==-5)say("Another network transfer is busy. Try again shortly.");
    else if(api->esc_pending())say("Stopped. The previous page has been kept.");
    else if(!ip)say("The server name could not be found. Check the address and DNS settings.");
    else if(result>0)api->kfmt(status,sizeof status,"Server returned HTTP %d. The previous page has been kept.",result);
    else say("Could not receive a complete page. Check the address or try again.");
    api->kfree(next);api->kfree(d.data);api->gui_dirty();
}
static void go(void)
{
    if(searching){
        char encoded[TW_URL],url[TW_URL];int n=(int)api->strlen(search_url);
        if(!address[0]){say("Enter some search words first.");return;}
        if(!tw_encoded(encoded,sizeof encoded,address)||n+3+(int)api->strlen(encoded)>=TW_URL){say("Search is too long.");return;}
        api->kfmt(url,sizeof url,"%s%%09%s",search_url,encoded);navigate(url,-1);
    }else navigate(address,-1);
}
static void follow(void){if(page.links&&selected>=0&&selected<page.links)navigate(page.link[selected].url,-1);}
static void home(void)
{
    if(hpos>=0&&!at_home)history[hpos].scroll=top;
    const char *welcome="Text Web\n\nEnter a gopher:// or http:// address above.\n\nGopher menus, text documents and searches are supported.\nHTTP pages show text and a separate list of links.\n\nClick Links to choose a destination. Use Back and Forward to revisit pages.\n\nThis reader has no HTTPS, images, scripts, forms or downloads.\nPages are limited to 64 KiB; long pages may be shortened for display.\n\nAddresses are kept in memory until restart. Nothing is fetched until you choose Go.";
    tw_render(&page,welcome,0,"");set_address("gopher://");field.all=1;current[0]=0;
    top=links_view=selected=searching=0;focus=at_home=1;say("Enter an address, then press Enter or Go.");
}
static void opened(int i){(void)i;if(!started){home();started=1;}}
static void size(int *w,int *h){*w=496;*h=328;}
static void initial(int i,int *w,int *h){(void)i;size(w,h);}
static int row_offset(int row,int cols){int p=0;for(int i=0;i<row&&page.text[p];i++)p=tw_wrap(page.text,p,cols);return p;}
static int total_rows(int cols){int p=0,n=0;while(page.text[p]){p=tw_wrap(page.text,p,cols);n++;}return n;}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;ui_header(x,y,cw,"Text Web");
    static const char *const labels[]={"Back","Forward","Home","Links","Copy"};
    for(int i=0;i<5;i++)ui_button(x,y,ui_r(12+i*78,34,72,22),labels[i],i==3&&links_view,i==0?back_target()>=0:i==1?!at_home&&hpos+1<hcount:i==3?page.links>0:1);
    api->draw_text(x+12,y+66,searching?"Search":"Address",C_GRAY);af_draw(&field,x+76,y+60,cw-148,focus);
    ui_button(x,y,ui_r(cw-66,60,54,24),"Go",0,1);
    int height=ch-120,cols=(cw-52)/8;view_rows=(height-6)/16;if(view_rows<1)view_rows=1;
    api->panel(x+12,y+92,cw-24,height,1);api->fill_rect(x+14,y+94,cw-28,height-4,C_WHITE);
    int total=links_view?page.links:total_rows(cols);if(top>total-view_rows)top=total-view_rows;if(top<0)top=0;
    if(links_view){
        for(int r=0;r<view_rows&&top+r<page.links;r++){
            int i=top+r,yy=y+95+r*16;char line[90];api->kfmt(line,sizeof line,"[%d] %s",i+1,page.link[i].label);
            if(i==selected)api->fill_rect(x+15,yy,cw-45,16,C_NAVY);
            api->draw_text_clip(x+18,yy,line,i==selected?C_WHITE:C_NAVY,cw-54);
        }
    }else{
        int off=row_offset(top,cols);
        for(int r=0;r<view_rows&&page.text[off];r++){
            int next=tw_wrap(page.text,off,cols),n=next-off;char line[128];
            if(n>127)n=127;while(n&&(page.text[off+n-1]=='\n'||page.text[off+n-1]==' '))n--;
            tw_copy(line,sizeof line,page.text+off,n);api->draw_text(x+18,y+95+r*16,line,C_BLACK);off=next;
        }
    }
    api->draw_sbar(x+cw-27,y+94,height-4,0,total,view_rows,top);
    ui_status(x,y,cw,ch,status);
}
static void scroll_by(int delta){top+=delta;if(top<0)top=0;}
static void key(int i,int k)
{
    (void)i;
    if(k==12){if(searching){searching=0;set_address(search_url);}focus=1;field.all=1;return;}
    if(k==27&&searching){searching=0;set_address(current);say("Search cancelled.");return;}
    if(k=='\t'){focus=!focus;return;}
    if(focus){if(k=='\n')go();else af_key(&field,k);return;}
    if(k==3){copy_text();return;}
    if(k==K_LEFT&&back_target()>=0){int target=back_target();navigate(history[target].url,target);return;}
    if(k==K_RIGHT&&!at_home&&hpos+1<hcount){navigate(history[hpos+1].url,hpos+1);return;}
    if(k=='l'&&page.links){links_view=!links_view;top=selected=0;return;}
    if(links_view){
        if(k==K_UP&&selected)selected--;else if(k==K_DOWN&&selected+1<page.links)selected++;
        else if(k=='\n'){follow();return;}
        if(selected<top)top=selected;if(selected>=top+view_rows)top=selected-view_rows+1;
        if(page.links)say(page.link[selected].url);
    }else if(k==K_UP)scroll_by(-1);else if(k==K_DOWN)scroll_by(1);else if(k==K_PGUP)scroll_by(-view_rows);else if(k==K_PGDN||k==' ')scroll_by(view_rows);else if(k==K_HOME)top=0;else if(k==K_END)top=TW_TEXT;
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;if(ev!=EV_PRESS)return;
    for(int n=0;n<5;n++)if(ui_hit(ui_r(12+n*78,34,72,22),x,y)){
        if(n==0&&back_target()>=0){int target=back_target();navigate(history[target].url,target);}
        else if(n==1&&!at_home&&hpos+1<hcount)navigate(history[hpos+1].url,hpos+1);
        else if(n==2)home();else if(n==3&&page.links){links_view=!links_view;top=selected=0;focus=0;}
        else if(n==4)copy_text();return;
    }
    if(ui_hit(ui_r(cw-66,60,54,24),x,y)){go();return;}
    focus=ui_hit(ui_r(76,60,cw-148,24),x,y);if(focus)return;
    if(x>=cw-28&&x<cw-12&&y>=94&&y<ch-28){int total=links_view?page.links:total_rows((cw-52)/8);top=api->sbar_from_pos(ch-124,total,view_rows,y-94);return;}
    if(links_view&&x>=14&&x<cw-28&&y>=95&&y<ch-28){int n=top+(y-95)/16;if(n<page.links){selected=n;follow();}}
}
static void wheel(int i,int dz){(void)i;scroll_by(dz*3);}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Text Web"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);static const AppDesc d={.title="Text Web",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .open=opened,.draw=draw,.key=key,.mouse=mouse,.wheel=wheel,.client_size=initial,.min_client=size,.live_draw=1};
    return k->register_app(&d)<0;
}
