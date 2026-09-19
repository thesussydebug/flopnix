#include "kapi.h"
#include "gdi.h"
#include "nettext.h"
#include "nethttp.h"
#include "browser_core.inc"
#include "browser_transfer.inc"
#include "buffer_core.inc"
#include "shspec.inc"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
static BrPage *page;
static int browser_type,alive;

static char address[TW_URL],current[TW_URL],search_url[TW_URL],status[128];
static AppField field;
typedef struct {char url[TW_URL];int scroll;} History;
static History history[8];
static int hcount,hpos=-1,top,links_view,selected=-1,focus=1,searching,at_home=1,view_rows=10,gopher,busy,closing,formatting;
static u16 line_marks[TW_TEXT/32+1];static int line_count,line_cols;
typedef struct {u8 *data;u32 len,cap,limit;int full;u32 tick;NetHttpInfo info;} Transfer;
static Transfer pending;
static char save_name[13],save_path[132];static int save_drive;
static void say(const char *s){api->strlcpy(status,s,sizeof status);api->gui_dirty();}
static int back_target(void){return at_home?hpos:hpos-1;}
static void set_address(const char *s){af_set(&field,address,sizeof address,s);}
static void release(Transfer *d){if(d->data)api->kfree(d->data);api->memset(d,0,sizeof *d);}
static void dispose(void)
{
    release(&pending);api->kfree(page);page=0;current[0]=0;line_cols=0;closing=0;at_home=1;
}
static int allocate(Transfer *d)
{
    api->memset(d,0,sizeof *d);d->limit=br_capacity(api->mem_total_kb());d->cap=4096;
    d->data=api->kmalloc(d->cap+1);
    if(!d->data){say("Not enough memory. Close another app and try again.");return 0;}
    d->data[0]=0;api->mem_track("Browser response",d->data,d->cap+1);return 1;
}
static int receive(const u8 *s,int n,void *ctx)
{
    Transfer *d=ctx;if(closing||api->esc_pending())return 0;
    if(n<0||(u32)n>d->limit-d->len){d->full=1;return 0;}
    u32 needed=d->len+(u32)n;
    if(needed>d->cap){
        u32 cap=buffer_capacity(d->cap,needed,4096,d->limit);
        u8 *next=api->krealloc(d->data,cap+1);
        if(!next&&cap>needed){cap=needed;next=api->krealloc(d->data,cap+1);}
        if(!next){d->full=2;return 0;}
        d->data=next;d->cap=cap;api->mem_track("Browser response",d->data,cap+1);
    }
    api->memcpy(d->data+d->len,s,(u32)n);d->len+=(u32)n;d->data[d->len]=0;
    if((u32)(*api->ticks-d->tick)>=10){d->tick=*api->ticks;api->kfmt(status,sizeof status,"Receiving %u KiB... Esc stops the transfer.",(d->len+1023)/1024);api->gui_dirty();}
    return 1;
}
static int fetch(char *url,TwUrl *u,Transfer *d)
{
    if(!api->net_up()||!api->net_get(NET_IP)){say("Network is not ready. Check Network in Settings.");return 0;}
    const NetHttpOps *http=api->service_get("net.http");const NetTextOps *text=api->service_get("net.text");
    api->esc_arm();
    for(int hop=0;hop<6;hop++){
        if(!tw_url(url,u)){say("This address needs HTTP or Gopher. HTTPS is not supported.");return 0;}
        if((u->http&&(!http||http->abi!=NET_HTTP_ABI||!http->get))||(!u->http&&(!text||text->abi!=NET_TEXT_ABI||!text->request))){say("Browser needs the matching net.kx extension.");return 0;}
        say(hop?"Following redirect... Esc stops the transfer.":"Connecting... Esc stops the transfer.");
        u32 ip=api->net_dns(u->host,400);if(closing||api->esc_pending()){say("Stopped. The previous page is still available.");return 0;}
        if(!ip){say("Server not found. Check the address and DNS settings.");return 0;}
        d->len=0;d->full=0;d->data[0]=0;api->memset(&d->info,0,sizeof d->info);int result;
        if(u->http){char host[90];if(u->port==80)api->strlcpy(host,u->host,sizeof host);else api->kfmt(host,sizeof host,"%s:%u",u->host,u->port);result=http->get(ip,u->port,host,u->path,receive,d,800,&d->info);}
        else {char request[180];api->kfmt(request,sizeof request,"%s\r\n",u->path);result=text->request(ip,u->port,request,receive,d,800);}
        if(closing||api->esc_pending()){say("Stopped. The previous page is still available.");return 0;}
        if(d->full==2){say("Not enough memory to finish the transfer. Nothing was saved.");return 0;}
        if(d->full){api->kfmt(status,sizeof status,"Transfer exceeds the %u KiB memory limit. Nothing was saved.",d->limit/1024);api->gui_dirty();return 0;}
        if(u->http&&br_redirect(result)){
            char next[TW_URL];if(hop==5){say("Too many redirects. Check the address.");return 0;}
            if(!tw_resolve(url,d->info.location,next)){say("Redirect needs an unsupported or invalid address (possibly HTTPS).");return 0;}
            api->strlcpy(url,next,TW_URL);continue;
        }
        if(result==(u->http?200:0)||(u->http&&result==204))return 1;
        if(result==-5)say("Another network transfer is busy. Try again shortly.");
        else if(result==-6)say("Server used unsupported compression. Nothing was saved.");
        else if(result>0){api->kfmt(status,sizeof status,"HTTP %d. The previous page is still available.",result);api->gui_dirty();}
        else say("The transfer was incomplete. Nothing was saved; try again.");
        return 0;
    }
    return 0;
}
static int read_local(const char *path,Transfer *d)
{
    d->limit=FS_MAXFILE;
    say("Reading local file...");api->esc_arm();
    for(;;){
        int n=path[0]=='u'?api->fat_read(path+2,d->data,d->cap):api->fs_read(path+2,d->data,d->cap);
        if(closing||api->esc_pending()){say("Stopped. The previous page is still available.");return 0;}
        if(n<0){say("Could not read that file. The previous page is still available.");return 0;}
        if((u32)n>d->limit){say("File exceeds this computer's Browser memory limit.");return 0;}
        if((u32)n<d->cap){d->len=(u32)n;d->data[n]=0;return 1;}
        u32 cap=buffer_capacity(d->cap,d->cap+1,4096,d->limit+1);
        u8 *next=api->krealloc(d->data,cap+1);
        if(!next){say("Not enough memory to read this file. Previous page retained.");return 0;}
        d->data=next;d->cap=cap;api->mem_track("Browser response",next,cap+1);
    }
}
static void save_picker(void);
static void choose_again(int result,void *ctx)
{
    (void)ctx;if(result==MBR_OK&&pending.data)save_picker();else release(&pending);
}
static void save_write(void)
{
    if(!pending.data)return;busy=1;api->busy_set("Browser","Saving download...",0);
    int r=save_drive?api->fat_write(save_path,pending.data,pending.len):api->fs_write(save_path,pending.data,pending.len);
    api->busy_end();busy=0;
    if(!r){api->kfmt(status,sizeof status,"Saved %u bytes to %s:%s",pending.len,save_drive?"u":"a",save_path);api->gui_dirty();api->broadcast("fs.changed",save_path);release(&pending);}
    else {
        say(r==-2?"The drive is full. Download is still in memory.":"Could not save the download. It is still in memory.");
        if(!closing)api->msgbox("Download not saved","Choose another destination? The downloaded data has been retained.",MB_OKCANCEL,choose_again,0);
    }
    if(closing)dispose();
}
static void overwrite(int result,void *ctx){(void)ctx;if(result==MBR_YES)save_write();else {release(&pending);say("Save cancelled.");}}
static void save_picked(const char *path,void *ctx)
{
    (void)ctx;if(!pending.data)return;
    if(!path){release(&pending);say("Save cancelled.");return;}
    if(!sp_resolve("",path,&save_drive,save_path,sizeof save_path)){api->msgbox("Browser","The file path is invalid. Choose another destination.",MB_OKCANCEL,choose_again,0);return;}
    if(!save_drive&&pending.len>FS_MAXFILE){api->msgbox("Browser","This file exceeds the 512 KiB browser save limit for A:. Choose a USB destination.",MB_OKCANCEL,choose_again,0);return;}
    if(save_drive&&!br_usb_name(save_path)){api->msgbox("Browser","Use a USB file name of up to 8 letters or numbers, with an extension of up to 3 characters.",MB_OKCANCEL,choose_again,0);return;}
    if(save_drive?api->fat_exists(save_path):api->fs_exists(save_path)){
        char msg[176];api->kfmt(msg,sizeof msg,"Replace the existing file %s:%s?",save_drive?"u":"a",save_path);api->msgbox("Browser",msg,MB_YESNO,overwrite,0);return;
    }
    save_write();
}
static void save_picker(void){api->file_save("Save download","",save_name,save_picked,0);}
static void offer_save(Transfer *d,const char *url,int html)
{
    release(&pending);br_filename(save_name,url,d->info.filename,html);api->memcpy(&pending,d,sizeof pending);d->data=0;
    api->mem_track("Browser download",pending.data,pending.cap+1);say("Choose a folder on A: or USB for the download.");save_picker();
}
static void visit(const char *url,int target,int download)
{
    if(busy||pending.data)return;char requested[TW_URL];TwUrl u;
    int local=tw_local(url,requested);
    if(!local){if(!tw_url(url,&u)){say("Enter HTTP, Gopher, or a local path such as a:page.html or u:/page.htm.");return;}api->strlcpy(requested,url,sizeof requested);}
    if(!local&&!download&&!u.http&&u.type=='7'&&!tw_contains(u.path,"\t")){
        api->strlcpy(search_url,requested,sizeof search_url);set_address("");searching=focus=1;say("Type your search words, then press Enter.");return;
    }
    Transfer d;if(!allocate(&d))return;busy=1;
    if(local?read_local(requested,&d):fetch(requested,&u,&d)){
        int mode=local?1:br_mode(&u,&d.info,d.data,(int)d.len);
        if(download||mode<0){if(!local&&!u.http&&u.type=='0')d.len=br_gopher_text(d.data,d.len);busy=0;offer_save(&d,requested,mode==1);}
        else {
            {
                BrPage *next=api->kmalloc(sizeof *next);BrCss *css=api->kmalloc(sizeof *css);
                if(!next||!css){api->kfree(next);api->kfree(css);say("Not enough memory to render. Previous page retained.");release(&d);busy=0;if(closing)dispose();return;}
                api->mem_track("Browser page",next,sizeof *next);api->mem_track("Browser CSS",css,sizeof *css);
                formatting=1;br_render(next,(const char *)d.data,mode,requested,css);api->kfree(css);
                if(hpos>=0&&!at_home)history[hpos].scroll=top;
                if(target>=0){hpos=target;top=history[hpos].scroll;api->strlcpy(history[hpos].url,requested,TW_URL);}
                else {hcount=hpos+1;if(hcount==8){api->memmove(history,history+1,7*sizeof history[0]);hcount--;}hpos=hcount++;api->strlcpy(history[hpos].url,requested,TW_URL);history[hpos].scroll=0;top=0;}
                api->kfree(page);page=next;line_cols=0;formatting=0;
                set_address(requested);api->strlcpy(current,requested,sizeof current);gopher=!local&&!u.http;links_view=focus=searching=at_home=0;selected=-1;
                api->kfmt(status,sizeof status,"%u bytes, %d links.%s",d.len,page->page.links,page->page.clipped?" Display limit reached.":"");
            }
        }
    }
    release(&d);busy=0;if(closing)dispose();api->gui_dirty();
}
static void picked(const char *path,void *ctx)
{
    (void)ctx;if(!alive||!path||busy||pending.data)return;
    char spec[TW_URL];
    if(path[0]&&path[1]==':')api->strlcpy(spec,path,sizeof spec);
    else api->kfmt(spec,sizeof spec,"a:%s",path);
    visit(spec,-1,0);
}
static void open_file(void){api->file_picker("Open HTML file (.html or .htm)",0,0,picked,0);}
static int html_opener(const char *name,const char *fullpath,const u8 *data,int n)
{
    (void)data;(void)n;if(busy||pending.data)return -1;
    char spec[TW_URL];api->kfmt(spec,sizeof spec,"%s:%s",fullpath?"u":"a",fullpath?fullpath:name);
    if(api->win_open(browser_type)<0)return -1;visit(spec,-1,0);return 0;
}
static void dropped(int i,int x,int y,const char *kind,const char *payload)
{
    (void)i;(void)x;(void)y;if(!kind||api->strcmp(kind,"file")||!payload)return;
    char spec[TW_URL];int n=0;while(payload[n]&&payload[n]!='\n'&&n<TW_URL-1){spec[n]=payload[n];n++;}
    if(payload[n]){say("Drop one HTML file at a time.");return;}spec[n]=0;picked(spec,0);
}
static void go(void)
{
    if(searching){
        char encoded[TW_URL],url[TW_URL];int n=tw_len(search_url);
        if(!address[0]){say("Enter some search words first.");return;}
        if(!tw_encoded(encoded,sizeof encoded,address)||n+3+tw_len(encoded)>=TW_URL){say("Search is too long.");return;}
        api->kfmt(url,sizeof url,"%s%%09%s",search_url,encoded);visit(url,-1,0);
    }else visit(address,-1,0);
}
static void follow(void){if(page&&selected>=0&&selected<page->page.links)visit(page->page.link[selected].url,-1,0);}
static void download(void)
{
    if(page&&selected>=0&&selected<page->page.links)visit(page->page.link[selected].url,-1,1);
    else if(current[0])visit(current,-1,1);
    else visit(address,-1,1);
}
static void copy_text(void)
{
    if(!page)return;const char *s=links_view&&selected>=0&&selected<page->page.links?page->page.link[selected].url:page->page.text;
    u32 n=api->strlen(s);if(n>4095)n=4095;
    if(api->clip_set("text",s,n)!=0)say("The clipboard is unavailable.");else say(api->strlen(s)>n?"Copied the first 4095 characters (clipboard limit).":"Copied to the clipboard.");
}
static void home(void)
{
    if(busy||pending.data)return;if(hpos>=0&&!at_home)history[hpos].scroll=top;
    if(!page){page=api->kmalloc(sizeof *page);if(!page){say("Not enough memory to open Browser.");return;}api->mem_track("Browser page",page,sizeof *page);}
    const char *welcome="Browser\n\nEnter an http:// or gopher:// address, or a local path such as a:page.html or u:/page.htm. Use Open or Ctrl+O to choose a file.\n\nClick a link to open it. Right-click a link to select it for Download; click blank page space to clear the selection.\n\nGopher menus, documents, searches and downloads are supported. The Links button is shown on Gopher pages.\n\nCtrl+L selects the address. Ctrl+C copies page text. Back and Forward revisit pages.\n\nBasic HTML and embedded or inline CSS are supported. HTTPS, scripts, forms, images and external stylesheets are not supported.\n\nDownload limits are up to 128 KiB at 4 MB, 256 KiB at 8 MB, and 1 MiB at 16 MB or more, depending on free memory. Local files can be opened up to 512 KiB. Downloads saved to A: can be up to 512 KiB.";
    br_render(page,welcome,0,"",0);set_address("http://");field.anchor=0;field.caret=field.len;current[0]=0;top=links_view=searching=gopher=0;selected=-1;focus=at_home=1;line_cols=0;say("Enter an address, then press Enter or Go.");
}
static void opened(int i){(void)i;alive=1;if(!page)home();}
static void closed(int i){(void)i;alive=0;if(busy){closing=1;return;}dispose();}
static void size(int *w,int *h){*w=584;*h=300;}
static void initial(int i,int *w,int *h){(void)i;*w=600;*h=328;}
static void layout(int cols)
{
    if(cols<1)cols=1;if(line_cols==cols)return;line_cols=cols;line_count=0;if(!page)return;
    int p=0;while(page->page.text[p]&&line_count<TW_TEXT){if(!(line_count&31))line_marks[line_count/32]=(u16)p;line_count++;p=tw_wrap(page->page.text,p,cols);}
}
static int row_offset(int row){if(row>=line_count)return page->page.len;int p=line_marks[row/32];for(int i=0;i<(row&31);i++)p=tw_wrap(page->page.text,p,line_cols);return p;}
static int line_end(int row){return row_offset(row+1);}
static int line_length(int row){int end=line_end(row),off=row_offset(row);while(end>off&&(page->page.text[end-1]=='\n'||page->page.text[end-1]==' '))end--;return end-off;}
static int line_shift(int row,int cols)
{
    int align=page->runs?page->run[br_run_at(page,row_offset(row))].style.align:0,space=cols-line_length(row);if(space<0)space=0;return align==1?space/2:align==2?space:0;
}
static UiRect button(int n,int cw){int count=gopher&&!at_home?7:6,w=(cw-24-(count-1)*6)/count;return ui_r(12+n*(w+6),8,w,22);}
static int enabled(int n){return !busy&&!pending.data&&(n==0?back_target()>=0:n==1?!at_home&&hpos+1<hcount:n==2?current[0]!=0:n==4?current[0]||address[0]:n==6?page&&page->page.links:1);}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    if(formatting)return;
    (void)w;static const char *const labels[]={"Back","Forward","Reload","Home","Download","Open","Links"};
    for(int i=0;i<(gopher&&!at_home?7:6);i++)ui_button(x,y,button(i,cw),labels[i],i==6&&links_view,enabled(i));
    api->draw_text(x+12,y+43,searching?"Search":"Address",C_GRAY);af_draw(&field,x+76,y+37,cw-148,focus);ui_button(x,y,ui_r(cw-66,37,54,24),"Go",0,!busy&&!pending.data);
    int height=ch-94,cols=(cw-52)/8;view_rows=(height-6)/16;if(view_rows<1)view_rows=1;layout(cols);
    api->panel(x+12,y+68,cw-24,height,1);api->fill_rect(x+14,y+70,cw-28,height-4,page&&!links_view?page->background:C_WHITE);
    int total=page?(links_view?page->page.links:line_count):0;if(top>total-view_rows)top=total-view_rows;if(top<0)top=0;
    if(page&&links_view){
        for(int r=0;r<view_rows&&top+r<page->page.links;r++){int i=top+r,yy=y+72+r*16;char line[90];api->kfmt(line,sizeof line,"[%d] %s",i+1,page->page.link[i].label);if(i==selected)api->fill_rect(x+15,yy,cw-45,16,C_NAVY);api->draw_text_clip(x+18,yy,line,i==selected?C_WHITE:C_NAVY,cw-54);}
    }else if(page){
        for(int row=top;row<top+view_rows&&row<line_count;row++){
            int off=row_offset(row),n=line_length(row),shift=line_shift(row,cols),yy=y+72+(row-top)*16,run=br_run_at(page,off);
            for(int c=0;c<n&&c+shift<cols;c++){
                while(run+1<page->runs&&page->run[run+1].start<=off+c)run++;
                BrStyle st=page->runs?page->run[run].style:(BrStyle){C_BLACK,C_WHITE,0,0};int link=page->runs?page->run[run].link:0,xx=x+18+(shift+c)*8;
                if(link&&link==selected+1){st.fg=C_WHITE;st.bg=C_NAVY;}
                if(st.bg!=page->background)api->fill_rect(xx,yy,8,16,st.bg);
                api->draw_char(xx,yy,page->page.text[off+c],st.fg);if(st.flags&1)api->draw_char(xx+1,yy,page->page.text[off+c],st.fg);if(st.flags&2)api->hline(xx,yy+14,8,st.fg);
            }
        }
    }
    api->draw_sbar(x+cw-27,y+70,height-4,0,total,view_rows,top);ui_status(x,y,cw,ch,status);
}
static void scroll_by(int delta){top+=delta;if(top<0)top=0;api->gui_dirty();}
static void select_link(void)
{
    if(!page||!page->page.links)return;selected=(selected+1)%page->page.links;
    if(links_view)top=selected;
    else for(int r=0;r<page->runs;r++)if(page->run[r].link==selected+1){int lo=0,hi=line_count;while(lo<hi){int mid=(lo+hi)/2;if(row_offset(mid)<=page->run[r].start)lo=mid+1;else hi=mid;}int row=lo?lo-1:0;if(row<top||row>=top+view_rows)top=row;break;}
    say(page->page.link[selected].url);
}
static void key(int i,int k)
{
    (void)i;if(busy||pending.data)return;
    if(k==15){open_file();return;}
    if(k==12){if(searching){searching=0;set_address(search_url);}focus=1;field.anchor=0;field.caret=field.len;return;}
    if(k==27){if(searching){searching=0;set_address(current);say("Search cancelled.");}else {selected=-1;focus=0;}return;}
    if(k=='\t'){focus=0;select_link();return;}
    if(focus){if(k=='\n')go();else af_key(&field,k);return;}
    if(k==3){copy_text();return;}if(k==19){download();return;}
    if(k==K_LEFT&&back_target()>=0){int t=back_target();visit(history[t].url,t,0);return;}
    if(k==K_RIGHT&&!at_home&&hpos+1<hcount){visit(history[hpos+1].url,hpos+1,0);return;}
    if(k=='l'&&gopher&&page&&page->page.links){links_view=!links_view;top=0;selected=0;return;}
    if(k=='\n'){follow();return;}
    if(links_view){if(k==K_UP&&selected>0)selected--;else if(k==K_DOWN&&page&&selected+1<page->page.links)selected++;if(selected<top)top=selected;if(selected>=top+view_rows)top=selected-view_rows+1;if(page&&selected>=0)say(page->page.link[selected].url);}
    else if(k==K_UP)scroll_by(-1);else if(k==K_DOWN)scroll_by(1);else if(k==K_PGUP)scroll_by(-view_rows);else if(k==K_PGDN||k==' ')scroll_by(view_rows);else if(k==K_HOME)top=0;else if(k==K_END)top=TW_TEXT;
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;if(busy||pending.data)return;
    if(af_mouse(&field,76,37,cw-148,x,y,ev)){focus=1;return;}
    if(ev==EV_PRESS)for(int n=0;n<(gopher&&!at_home?7:6);n++)if(ui_hit(button(n,cw),x,y)){
        if(!enabled(n))return;if(n==0){int t=back_target();visit(history[t].url,t,0);}else if(n==1)visit(history[hpos+1].url,hpos+1,0);else if(n==2)visit(current,hpos,0);else if(n==3)home();else if(n==4)download();else if(n==5)open_file();else {links_view=!links_view;top=0;selected=0;focus=0;}return;
    }
    if(ev!=EV_PRESS&&ev!=EV_RPRESS&&ev!=EV_DRAG)return;
    if(ev==EV_PRESS&&ui_hit(ui_r(cw-66,37,54,24),x,y)){go();return;}
    if(ev==EV_PRESS){focus=ui_hit(ui_r(76,37,cw-148,24),x,y);if(focus)return;}
    int cols=(cw-52)/8;layout(cols);
    if(x>=cw-28&&x<cw-12&&y>=70&&y<ch-26){int total=page?(links_view?page->page.links:line_count):0;top=api->sbar_from_pos(ch-98,total,view_rows,y-70);return;}
    if(ev==EV_DRAG||!page||x<18||x>=cw-32||y<72||y>=72+view_rows*16)return;
    int row=top+(y-72)/16,link=-1;
    if(links_view){if(row<page->page.links)link=row;}
    else if(row<line_count){int col=(x-18)/8-line_shift(row,cols);if(col>=0&&col<line_length(row)&&page->runs)link=page->run[br_run_at(page,row_offset(row)+col)].link-1;}
    selected=link;
    if(link>=0){if(ev==EV_PRESS)follow();else say(page->page.link[link].url);}else say("Ready. Ctrl+C copies page text.");
}
static void wheel(int i,int dz){(void)i;if(!busy)scroll_by(-dz*3);}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"Browser"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;
    api=k;ui_init(k,0);static const AppDesc d={.title="Browser",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .open=opened,.close=closed,.draw=draw,.key=key,.mouse=mouse,.wheel=wheel,.drop=dropped,.client_size=initial,.min_client=size,.live_draw=APP_INDEPENDENT};
    browser_type=k->register_app(&d);if(browser_type<0)return 1;
    return k->register_opener("html",html_opener)<0||k->register_opener("htm",html_opener)<0;
}
