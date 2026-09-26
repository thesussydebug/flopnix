#include "kapi.h"
#include "gdi.h"
#include "nettext.h"
#include "nethttp.h"
#include "browser_core.inc"
#include "browser_transfer.inc"
#include "browser_layout.inc"
#include "buffer_core.inc"
#include "fileopen.inc"
#include "shspec.inc"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
#include "browser_cache.inc"
static BrCache cache;static int reload_all,fetch_status;
static void *cache_alloc(u32 n){return api->kmalloc(n);}
static void cache_release(void *p){api->kfree(p);}
static void *br_malloc(u32 n){void *p;while(!(p=api->kmalloc(n))&&bc_evict_one(&cache)){}return p;}
static void *br_zalloc(u32 n){void *p=br_malloc(n);if(p)api->memset(p,0,n);return p;}
static void *br_realloc(void *p,u32 n)
{
    if(!n){api->kfree(p);return 0;}if(!p)return br_malloc(n);
    void *r;while(!(r=api->krealloc(p,n))&&bc_evict_one(&cache)){}return r;
}
#include "browser_image.inc"
typedef struct BrView BrView;
typedef struct {u8 *pixels;int w,h;BrView *child;} BrMedia;
struct BrView {BrPage *doc;BrLayout *layout;BrMedia media[BR_OBJECTS];BrView *parent;char url[TW_URL];int scroll,depth,post,viewport;};
static BrView root_view,*active_view=&root_view;
static unsigned nav_serial;
static int active_control=-1;static AppField control_field;
static u32 media_used;static int resource_count,document_count,resource_failures;
static void view_free(BrView *v);
#define top root_view.scroll
static BrPage *page;
static int browser_type,alive;

static char address[TW_URL],current[TW_URL],search_url[TW_URL],status[128];
static AppField field;
typedef struct {char url[TW_URL];int scroll;} History;
static History history[8];
static int hcount,hpos=-1,links_view,selected=-1,focus=1,searching,at_home=1,view_rows=10,gopher,busy,closing,formatting;
static int line_cols;
typedef struct {u8 *data;u32 len,cap;int full;u32 tick;NetHttpInfo info;} Transfer;
static Transfer pending;
static char save_name[13],save_path[132];static int save_drive;
static void say(const char *s){api->strlcpy(status,s,sizeof status);api->gui_dirty();}
static int back_target(void){return at_home?hpos:hpos-1;}
static void set_address(const char *s){af_set(&field,address,sizeof address,s);}
static void release(Transfer *d){if(d->data)api->kfree(d->data);api->memset(d,0,sizeof *d);}
static void dispose(void)
{
    nav_serial++;release(&pending);view_free(&root_view);bc_clear(&cache);page=0;active_view=&root_view;active_control=-1;current[0]=0;line_cols=0;closing=0;at_home=1;
}
static int allocate(Transfer *d)
{
    api->memset(d,0,sizeof *d);d->cap=4096;
    d->data=br_malloc(d->cap+1);
    if(!d->data){say("Not enough memory. Close another app and try again.");return 0;}
    d->data[0]=0;api->mem_track("Browser response",d->data,d->cap+1);return 1;
}
static int receive(const u8 *s,int n,void *ctx)
{
    Transfer *d=ctx;if(closing||api->esc_pending())return 0;
    if(n<0||d->len>FO_LIMIT||(u32)n>FO_LIMIT-d->len){d->full=1;return 0;}
    u32 needed=d->len+(u32)n;
    if(needed>d->cap){
        u32 cap=buffer_capacity(d->cap,needed,4096,FO_LIMIT);
        u8 *next=br_realloc(d->data,cap+1);
        if(!next&&cap>needed){cap=needed;next=br_realloc(d->data,cap+1);}
        if(!next){d->full=2;return 0;}
        d->data=next;d->cap=cap;api->mem_track("Browser response",d->data,cap+1);
    }
    api->memcpy(d->data+d->len,s,(u32)n);d->len+=(u32)n;d->data[d->len]=0;
    if((u32)(*api->ticks-d->tick)>=10){d->tick=*api->ticks;u32 total=d->info.length_known?d->info.length:0;
        if(total)api->kfmt(status,sizeof status,"Receiving %u of %u KiB... Esc stops",(d->len+1023)/1024,(total+1023)/1024);
        else api->kfmt(status,sizeof status,"Receiving %u KiB... Esc stops",(d->len+1023)/1024);
        int frac=total?(int)(d->len/((total+255)/256)):-1;api->busy_set("Browser",status,frac>256?256:frac);}
    return 1;
}
static int fetch(char *url,TwUrl *u,Transfer *d,const char *body)
{
    if(!api->net_up()||!api->net_get(NET_IP)){say("Network is not ready. Check Network in Settings.");return 0;}
    const NetHttpFormOps *forms=api->service_get("net.http.form");
    const NetHttpOps *http=api->service_get("net.http");const NetTextOps *text=api->service_get("net.text");
    api->esc_arm();
    for(int hop=0;hop<6;hop++){
        if(!tw_url(url,u)){say("This address needs HTTP or Gopher. HTTPS is not supported.");return 0;}
        if((u->http&&(!http||http->abi!=NET_HTTP_ABI||!http->get))||(!u->http&&(!text||text->abi!=NET_TEXT_ABI||!text->request))){say("Browser needs the matching net.kx extension.");return 0;}
        say(hop?"Following redirect... Esc stops the transfer.":"Connecting... Esc stops the transfer.");
        u32 ip=api->net_dns(u->host,400);if(closing||api->esc_pending()){say("Stopped. The previous page is still available.");return 0;}
        if(!ip){say("Server not found. Check the address and DNS settings.");return 0;}
        d->len=0;d->full=0;d->data[0]=0;api->memset(&d->info,0,sizeof d->info);int result;
        if(u->http){char host[90];if(u->port==80)api->strlcpy(host,u->host,sizeof host);else api->kfmt(host,sizeof host,"%s:%u",u->host,u->port);if(body){if(!forms||forms->abi!=NET_HTTP_FORM_ABI||!forms->post){say("POST forms need the matching net.kx extension.");return 0;}result=forms->post(ip,u->port,host,u->path,body,receive,d,800,&d->info);}else result=http->get(ip,u->port,host,u->path,receive,d,800,&d->info);}
        else {char request[180];api->kfmt(request,sizeof request,"%s\r\n",u->path);result=text->request(ip,u->port,request,receive,d,800);}
        api->busy_end();fetch_status=result;
        if(closing||api->esc_pending()){say("Stopped. The previous page is still available.");return 0;}
        if(d->full==2){say("Not enough memory to finish the transfer. Nothing was saved.");return 0;}
        if(d->full){say("Transfer is too large for this system. Nothing was saved.");return 0;}
        if(u->http&&br_redirect(result)){
            char next[TW_URL];if(hop==5){say("Too many redirects. Check the address.");return 0;}
            if(!tw_resolve(url,d->info.location,next)){say("Redirect needs an unsupported or invalid address (possibly HTTPS).");return 0;}
            if(body&&(result==301||result==302||result==303))body=0;
            if(body&&(!tw_url(next,u)||!u->http)){say("POST redirect requires HTTP.");return 0;}
            api->strlcpy(url,next,TW_URL);continue;
        }
        if((u->http&&result>=200&&result<206)||(!u->http&&result==0))return 1;
        if(result==-5)say("Another network transfer is busy. Try again shortly.");
        else if(result==-6)say("Server used unsupported compression. Nothing was saved.");
        else if(result>0){api->kfmt(status,sizeof status,"HTTP %d. The previous page is still available.",result);api->gui_dirty();}
        else say("The transfer was incomplete. Nothing was saved; try again.");
        return 0;
    }
    return 0;
}
static int local_cancelled(void){return closing||api->esc_pending();}
static int read_local(const char *path,Transfer *d)
{
    say("Reading local file...");api->esc_arm();release(d);
    FileData file;int r=fo_load(api,path[0]=='u',path+2,1,&file,local_cancelled);
    if(r){say(r==FO_CANCELLED?"Stopped. The previous page is still available.":fo_error(r));return 0;}
    d->data=file.data;d->len=file.size;d->cap=file.capacity-1;
    api->mem_track("Browser response",d->data,d->cap+1);return 1;
}
static int page_cached(const char *url,Transfer *d)
{
    BcEntry *e=bc_find(&cache,url,BC_PAGE);if(!e)return -1;u32 n=e->size;u8 *p=br_malloc(n+1);
    if(p&&!((e=bc_find(&cache,url,BC_PAGE))&&e->size==n)){api->kfree(p);p=0;}if(!p)return -1;
    api->memcpy(p,e->data,n);p[n]=0;release(d);d->data=p;d->len=d->cap=n;api->mem_track("Browser response",p,n+1);return e->a;
}
static int target_url(const char *url,char *requested,char *localpath,int *local,TwUrl *u)
{
    int fragment=0;while(url[fragment]&&url[fragment]!='#')fragment++;if(!tw_copy(localpath,TW_URL,url,fragment))return 0;
    *local=tw_local(localpath,requested);if(*local){api->strlcpy(localpath,requested,TW_URL);int n=tw_len(requested);if(url[fragment]&&n+tw_len(url+fragment)<TW_URL)tw_copy(requested+n,TW_URL-n,url+fragment,tw_len(url+fragment));return 1;}
    if(!tw_url(url,u))return 0;api->strlcpy(requested,url,TW_URL);return 1;
}
static int refresh_next(const BrPage *doc,const char *requested,char *next)
{
    if(!doc||!doc->refresh[0]||doc->refresh_delay>1)return 0;
    int n=0,m=0;while(requested[n]&&requested[n]!='#')n++;while(doc->refresh[m]&&doc->refresh[m]!='#')m++;
    if(n==m){int i=0;while(i<n&&requested[i]==doc->refresh[i])i++;if(i==n)return 0;}
    api->strlcpy(next,doc->refresh,TW_URL);return 1;
}
static int follow_refresh(const BrPage *doc,int hops,char *requested,char *localpath,int *local,TwUrl *u,Transfer *d)
{
    char forward[TW_URL],nreq[TW_URL],nlocal[TW_URL];TwUrl nu;int nl;
    if(!refresh_next(doc,requested,forward))return 0;
    if(hops>=4){say("Too many page refreshes. Showing the last page.");return -1;}
    if(!target_url(forward,nreq,nlocal,&nl,&nu)){say("This page forwards to an unsupported address (possibly HTTPS).");return -1;}
    if(!(nl?read_local(nlocal,d):fetch(nreq,&nu,d,0)))return -1;
    api->strlcpy(requested,nreq,TW_URL);api->strlcpy(localpath,nlocal,TW_URL);*local=nl;api->memcpy(u,&nu,sizeof nu);return 1;
}
#include "browser_view.inc"
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
    if(busy||pending.data)return;if(!download&&page&&view_anchor(&root_view,url)){api->gui_dirty();return;}char requested[TW_URL],localpath[TW_URL];TwUrl u;int local;
    if(!target_url(url,requested,localpath,&local,&u)){say("Enter HTTP, Gopher, or a local path such as a:page.html or u:/page.htm.");return;}
    if(!local&&!download&&!u.http&&u.type=='7'&&!tw_contains(u.path,"\t")){
        api->strlcpy(search_url,requested,sizeof search_url);set_address("");searching=focus=1;say("Type your search words, then press Enter.");return;
    }
    Transfer d;if(!allocate(&d))return;busy=1;int cached=!local&&!download&&target>=0&&!reload_all?page_cached(requested,&d):-1;
    if(cached>=0||(local?read_local(localpath,&d):fetch(requested,&u,&d,0)))for(int hops=0;;hops++){
        int mode=cached>=0?cached:local?1:br_mode(&u,&d.info,d.data,(int)d.len);
        if(download||mode<0){if(!local&&!u.http&&u.type=='0')d.len=br_gopher_text(d.data,d.len);busy=0;offer_save(&d,requested,mode==1);}
        else {
            br_text_fix(d.data,d.len);
            {
                BrPage *next=br_zalloc(sizeof *next);BrCss *css=br_malloc(sizeof *css);
                if(!next||!css){api->kfree(next);api->kfree(css);say("Not enough memory to render. Previous page retained.");release(&d);busy=0;if(closing)dispose();return;}
                api->mem_track("Browser page",next,sizeof *next);api->mem_track("Browser CSS",css,sizeof *css);
                BrView loaded;api->memset(&loaded,0,sizeof loaded);loaded.doc=next;loaded.layout=br_zalloc(sizeof *loaded.layout);
                if(!loaded.layout){api->kfree(next);api->kfree(css);say("Not enough memory for page layout.");release(&d);busy=0;if(closing)dispose();return;}
                loaded.layout->width=0;tw_copy(loaded.url,TW_URL,requested,tw_len(requested));
                resource_count=resource_failures=0;document_count=1;view_render(&loaded,(const char *)d.data,mode,css);api->kfree(css);
                if(closing){view_free(&loaded);release(&d);busy=0;dispose();return;}
                int forward=follow_refresh(loaded.doc,hops,requested,localpath,&local,&u,&d);
                if(closing){view_free(&loaded);release(&d);busy=0;dispose();return;}
                if(forward>0){view_free(&loaded);cached=-1;continue;}
                if(hpos>=0&&!at_home)history[hpos].scroll=top;
                int scroll=0;
                if(target>=0){hpos=target;scroll=history[hpos].scroll;api->strlcpy(history[hpos].url,requested,TW_URL);}
                else {hcount=hpos+1;if(hcount==8){api->memmove(history,history+1,7*sizeof history[0]);hcount--;}hpos=hcount++;api->strlcpy(history[hpos].url,requested,TW_URL);history[hpos].scroll=0;}
                formatting=1;nav_serial++;view_free(&root_view);api->memcpy(&root_view,&loaded,sizeof loaded);view_reparent(&root_view);page=root_view.doc;top=scroll;line_cols=0;formatting=0;active_view=&root_view;active_control=-1;
                set_address(requested);api->strlcpy(current,requested,sizeof current);gopher=!local&&!u.http;links_view=focus=searching=at_home=0;selected=-1;view_anchor(&root_view,requested);
                if(!forward)api->kfmt(status,sizeof status,"%u bytes%s, %d links, %d images/frames unavailable.%s",d.len,cached>=0?" from cache":"",page->page.links,resource_failures,page->page.clipped?" Display limit reached.":"");
                if(!local&&cached<0&&d.len)bc_put(&cache,requested,BC_PAGE,d.data,d.len,mode,0);
            }
        }
        break;
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
static void navigate_view(BrView *v,const char *url,const char *body)
{
    if(!body&&view_anchor(v,url)){api->gui_dirty();return;}
    if(v==&root_view&&!body){visit(url,-1,0);return;}
    char copy[TW_URL];if(!tw_copy(copy,sizeof copy,url,tw_len(url)))return;
    busy=1;resource_count=resource_failures=0;document_count=1;api->esc_arm();
    int ok=view_load(v,copy,body);busy=0;if(closing){dispose();return;}
    if(ok){active_view=v;active_control=-1;selected=-1;focus=0;
        if(v==&root_view){page=v->doc;set_address(v->url);api->strlcpy(current,v->url,sizeof current);at_home=searching=links_view=gopher=0;
            hcount=hpos+1;if(hcount==8){api->memmove(history,history+1,7*sizeof history[0]);hcount--;}hpos=hcount++;api->strlcpy(history[hpos].url,v->url,TW_URL);history[hpos].scroll=0;}
        say("Page loaded.");}
    api->gui_dirty();
}
static void follow(void)
{
    BrView *v=active_view;if(!v||!v->doc||selected<0||selected>=v->doc->page.links)return;
    int link=selected;char url[TW_URL];api->strlcpy(url,v->doc->page.link[link].url,sizeof url);
    if(v->doc->page.link[link].download)visit(url,-1,1);else navigate_view(view_target(v,v->doc->page.link[link].target),url,0);
}
static void download(void)
{
    BrPage *doc=active_view&&active_view->doc?active_view->doc:page;
    if(doc&&selected>=0&&selected<doc->page.links)visit(doc->page.link[selected].url,-1,1);
    else if(active_view&&active_view->url[0])visit(active_view->url,-1,1);
    else if(current[0])visit(current,-1,1);else visit(address,-1,1);
}
static void control_focus(BrView *v,int ci)
{
    active_view=v;active_control=ci;selected=-1;focus=0;BrControl *c=&v->doc->control[ci];af_set(&control_field,c->value,sizeof c->value,c->value);api->gui_dirty();
}
static void submit_form(BrView *v,int form,int button)
{
    if(form<0||form>=v->doc->forms){say("This control is not inside a form.");return;}BrForm *f=&v->doc->form[form];
    if(f->unsupported){say(f->unsupported&2?"This form exceeds Browser field or option limits; nothing was submitted.":"This form needs unsupported multipart or file-upload encoding.");return;}
    static char data[4097];char url[TW_URL];if(!br_form_data(v->doc,form,button,data,sizeof data)){say("Form data exceeds the 4 KiB submission limit.");return;}
    TwUrl u;if(!tw_url(f->action,&u)||!u.http){say("This form needs an HTTP action address.");return;}
    int n=0;while(f->action[n]&&f->action[n]!='#'&&(f->post||f->action[n]!='?'))n++;tw_copy(url,sizeof url,f->action,n);
    if(!f->post){int len=tw_len(data);if(n+1+len>=TW_URL){say("Search is too long for this browser's address limit.");return;}url[n++]='?';tw_copy(url+n,TW_URL-n,data,len);if(!tw_url(url,&u)){say("Search is too long for this browser's address limit.");return;}}
    BrView *target=view_target(v,f->target);navigate_view(target,url,f->post?data:0);
}
static const char *select_items[12];static int select_map[12],select_count,select_start,select_ci;static BrView *select_view;static unsigned select_serial;
static void select_popup(void);
static void select_picked(int index,void *ctx)
{
    (void)ctx;if(!alive||select_serial!=nav_serial||index<0||index>=select_count)return;
    if(select_map[index]<0){select_popup();return;}BrControl *c=&select_view->doc->control[select_ci];c->choice=select_map[index];api->gui_dirty();
}
static void select_popup(void)
{
    BrControl *c=&select_view->doc->control[select_ci];select_count=0;int k=select_start;
    while(k<c->count&&select_count<11){BrOption *o=&select_view->doc->option[c->option+k];if(!o->disabled){select_items[select_count]=o->label;select_map[select_count++]=k;}k++;}
    if(k<c->count){select_items[select_count]="More...";select_map[select_count++]=-1;select_start=k;}
    if(select_count)api->menu_show(*api->mouse_x,*api->mouse_y,select_items,select_count,select_picked,0);
}
static void activate_control(BrView *v,int ci,int row)
{
    BrControl *c=&v->doc->control[ci];if(c->disabled)return;control_focus(v,ci);
    if(c->type==BC_CHECK)c->checked=!c->checked;
    else if(c->type==BC_RADIO){for(int i=0;i<v->doc->controls;i++){BrControl *other=&v->doc->control[i];if(other->type==BC_RADIO&&other->form==c->form&&tw_eq(other->name,c->name))other->checked=0;}c->checked=1;}
    else if(c->type==BC_RESET){br_form_reset(v->doc,c->form);active_control=-1;}
    else if(c->type==BC_SUBMIT)submit_form(v,c->form,ci);
    else if(c->type==BC_SELECT){if(c->multiple){row+=br_max(0,c->choice)/5*5;if(row>=0&&row<c->count&&!v->doc->option[c->option+row].disabled){c->choice=row;v->doc->option[c->option+row].selected^=1;}}else for(int i=1;i<=c->count;i++){int next=(c->choice+i)%c->count;if(!v->doc->option[c->option+next].disabled){c->choice=next;break;}}}
    api->gui_dirty();
}
static void copy_text(void)
{
    if(!page)return;BrPage *doc=active_view&&active_view->doc?active_view->doc:page;const char *s=links_view&&selected>=0&&selected<doc->page.links?doc->page.link[selected].url:doc->page.text;
    char copy[4096];u32 n=0;for(int i=0;s[i]&&n<4095;i++)if(s[i]!=1)copy[n++]=s[i];copy[n]=0;
    if(api->clip_set("text",copy,n)!=0)say("The clipboard is unavailable.");else say(api->strlen(s)>n?"Copied the first 4095 characters (clipboard limit).":"Copied to the clipboard.");
}
static void home(void)
{
    if(busy||pending.data)return;if(hpos>=0&&!at_home)history[hpos].scroll=top;
    const char *welcome="Browser\n\nEnter an http:// or gopher:// address, or a local path such as a:page.html or u:/page.htm. Use Open or Ctrl+O to choose a file.\n\nClick a link to open it. Right-click a link to select it for Download; click blank page space to clear the selection.\n\nGopher menus, documents, searches and downloads are supported. The Links button is shown on Gopher pages.\n\nCtrl+L selects the address. Ctrl+C copies page text. Back and Forward revisit pages.\n\nBasic HTML and embedded or inline CSS are supported. Images, tables, frames, forms and external stylesheets are supported. HTTPS, scripts and file uploads are not supported. GIFs display their first frame.\n\nPages and downloads grow as available memory permits. Local files can be opened as available memory permits. Downloads saved to A: can be up to 512 KiB.";
    formatting=1;nav_serial++;view_free(&root_view);root_view.doc=page=br_zalloc(sizeof *page);root_view.layout=br_zalloc(sizeof *root_view.layout);if(!page||!root_view.layout){view_free(&root_view);page=0;formatting=0;say("Not enough memory to open Browser.");return;}root_view.layout->width=0;active_view=&root_view;active_control=-1;br_render(page,welcome,0,"",0);formatting=0;set_address("http://");field.anchor=0;field.caret=field.len;current[0]=0;top=links_view=searching=gopher=0;selected=-1;focus=at_home=1;line_cols=0;say("Enter an address, then press Enter or Go.");
}
static void opened(int i){(void)i;alive=1;if(!page)home();}
static void closed(int i){(void)i;alive=0;if(busy){closing=1;return;}dispose();}
static void size(int *w,int *h){*w=584;*h=300;}
static void initial(int i,int *w,int *h){(void)i;*w=600;*h=328;}
static UiRect button(int n,int cw){int count=gopher&&!at_home?7:6,w=(cw-24-(count-1)*6)/count;return ui_r(12+n*(w+6),8,w,22);}
static int enabled(int n){return !busy&&!pending.data&&(n==0?back_target()>=0:n==1?!at_home&&hpos+1<hcount:n==2?current[0]!=0:n==4?current[0]||address[0]:n==6?page&&page->page.links:1);}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    if(formatting)return;
    (void)w;static const char *const labels[]={"Back","Forward","Reload","Home","Download","Open","Links"};
    for(int i=0;i<(gopher&&!at_home?7:6);i++)ui_button(x,y,button(i,cw),labels[i],i==6&&links_view,enabled(i));
    api->draw_text(x+12,y+43,searching?"Search":"Address",C_GRAY);af_draw(&field,x+76,y+37,cw-148,focus);ui_button(x,y,ui_r(cw-66,37,54,24),"Go",0,!busy&&!pending.data);
    int height=ch-94;view_rows=height-6;
    api->panel(x+12,y+68,cw-24,height,1);
    if(page&&links_view){view_rows=(height-6)/16;top=br_max(0,br_min(top,page->page.links-view_rows));api->fill_rect(x+14,y+70,cw-28,height-4,C_WHITE);
        for(int r=0;r<view_rows&&top+r<page->page.links;r++){int i=top+r,yy=y+72+r*16;char line[90];api->kfmt(line,sizeof line,"[%d] %s",i+1,page->page.link[i].label);if(i==selected)api->fill_rect(x+15,yy,cw-45,16,C_NAVY);api->draw_text_clip(x+18,yy,line,i==selected?C_WHITE:C_NAVY,cw-54);}
        api->draw_sbar(x+cw-27,y+70,height-4,0,page->page.links,view_rows,top);
    }else if(page){view_draw(&root_view,x+18,y+72,cw-52,height-8,(BrClip){x+14,y+70,cw-44,height-4});api->draw_sbar(x+cw-27,y+70,height-4,0,root_view.layout->height,height-8,top);}
    ui_status(x,y,cw,ch,!busy&&!pending.data&&root_view.layout&&root_view.layout->clipped?"Display limit reached. Download saves the complete document.":status);
}
static void scroll_by(int delta){BrView *v=active_view?active_view:&root_view;v->scroll=br_max(0,v->scroll+delta);api->win_redraw(browser_type,0);}
static void select_next(void)
{
    BrView *v=active_view;if(!v||!v->doc||!v->layout)return;
    if(links_view){if(page->page.links){selected=(selected+1)%page->page.links;top=selected;}return;}
    int off=-1;if(active_control>=0){int oi=v->doc->control[active_control].object;if(oi>=0)off=(int)v->doc->object[oi].start;}
    else if(selected>=0)for(int i=0;i<v->doc->runs;i++)if(v->doc->run[i].link==selected+1){off=(int)v->doc->run[i].start;break;}
    for(int pass=0;pass<2;pass++){for(int i=0;i<v->layout->count;i++){BrBox *b=&v->layout->box[i];if((int)b->start<=off||b->object<=-2)continue;
        if(b->object>=0){BrObject *o=&v->doc->object[b->object];if(o->kind==BR_CONTROL&&!v->doc->control[o->ref].disabled){control_focus(v,o->ref);v->scroll=br_max(0,b->y-16);return;}}
        int link=b->object>=0?v->doc->object[b->object].link:v->doc->runs?v->doc->run[br_run_at(v->doc,b->start)].link:0;
        if(link&&link!=selected+1){selected=link-1;active_control=-1;v->scroll=br_max(0,b->y-16);say(v->doc->page.link[selected].url);return;}
    }off=-1;selected=-1;}
}
static void key(int i,int k)
{
    (void)i;if(busy||pending.data)return;
    if(k==15){open_file();return;}
    if(k==12){if(searching){searching=0;set_address(search_url);}active_control=-1;focus=1;field.anchor=0;field.caret=field.len;return;}
    if(k==27){if(searching){searching=0;set_address(current);say("Search cancelled.");}else {selected=-1;focus=0;active_control=-1;}return;}
    if(k=='\t'){focus=0;select_next();return;}
    if(focus){if(k=='\n')go();else af_key(&field,k);return;}
    if(active_control>=0&&active_view&&active_view->doc){BrControl *c=&active_view->doc->control[active_control];
        if(c->type==BC_TEXT||c->type==BC_PASSWORD||c->type==BC_AREA){if(k=='\n'&&c->type!=BC_AREA){int submit=-1;for(int n=0;n<active_view->doc->controls;n++)if(active_view->doc->control[n].form==c->form&&active_view->doc->control[n].type==BC_SUBMIT&&!active_view->doc->control[n].disabled){submit=n;break;}submit_form(active_view,c->form,submit);return;}
            TextEdit t={c->value,c->limit+1,control_field.len,control_field.caret,control_field.anchor};if(t.cap<t.len+1)t.cap=t.len+1;int result=at_key(&t,k,c->type==BC_AREA,c->readonly,0);if(result<0)say("Text exceeds this field's length limit.");control_field.len=t.len;control_field.caret=t.caret;control_field.anchor=t.anchor;return;}
        if(c->type==BC_SELECT&&(k==K_DOWN||k==K_UP)){for(int i=1;i<=c->count;i++){int next=(c->choice+(k==K_UP?-i:i)+c->count*2)%c->count;if(!active_view->doc->option[c->option+next].disabled){c->choice=next;break;}}return;}
        if(k==' '||k=='\n'){activate_control(active_view,active_control,c->multiple?br_max(0,c->choice)%5:0);return;}
    }
    if(k==3){copy_text();return;}if(k==19){download();return;}
    if(k==K_LEFT&&back_target()>=0){int t=back_target();visit(history[t].url,t,0);return;}
    if(k==K_RIGHT&&!at_home&&hpos+1<hcount){visit(history[hpos+1].url,hpos+1,0);return;}
    if(k=='l'&&gopher&&page&&page->page.links){links_view=!links_view;top=0;selected=0;return;}
    if(k=='\n'){follow();return;}
    if(links_view){if(k==K_UP&&selected>0)selected--;else if(k==K_DOWN&&page&&selected+1<page->page.links)selected++;if(selected<top)top=selected;if(selected>=top+view_rows)top=selected-view_rows+1;if(page&&selected>=0)say(page->page.link[selected].url);}
    else if(k==K_UP)scroll_by(-16);else if(k==K_DOWN)scroll_by(16);else if(k==K_PGUP)scroll_by(-view_rows);else if(k==K_PGDN||k==' ')scroll_by(view_rows);else if(k==K_HOME)active_view->scroll=0;else if(k==K_END&&active_view&&active_view->layout)active_view->scroll=active_view->layout->height;
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;if(busy||pending.data)return;
    if(af_mouse(&field,76,37,cw-148,x,y,ev)){focus=1;active_control=-1;return;}
    if(ev==EV_PRESS)for(int n=0;n<(gopher&&!at_home?7:6);n++)if(ui_hit(button(n,cw),x,y)){
        if(!enabled(n))return;if(n==0){int t=back_target();visit(history[t].url,t,0);}else if(n==1)visit(history[hpos+1].url,hpos+1,0);else if(n==2){if(root_view.post)say("Use the form's Submit button to send it again.");else {reload_all=1;visit(current,hpos,0);reload_all=0;}}else if(n==3)home();else if(n==4)download();else if(n==5)open_file();else {links_view=!links_view;top=0;selected=0;focus=0;}return;
    }
    if(ev!=EV_PRESS&&ev!=EV_RPRESS&&ev!=EV_DRAG)return;
    if(ev==EV_PRESS&&ui_hit(ui_r(cw-66,37,54,24),x,y)){go();return;}
    if(x>=cw-28&&x<cw-12&&y>=70&&y<ch-26){int total=page?(links_view?page->page.links:root_view.layout->height):0;top=api->sbar_from_pos(ch-98,total,view_rows,y-70);return;}
    if(!page||x<18||x>=cw-32||y<72||y>=ch-28)return;
    if(links_view){if(ev!=EV_PRESS&&ev!=EV_RPRESS)return;int row=top+(y-72)/16;selected=row<page->page.links?row:-1;active_view=&root_view;focus=0;active_control=-1;if(selected>=0){if(ev==EV_PRESS)follow();else say(page->page.link[selected].url);}return;}
    BrView *v=&root_view;int mx=x,my=y;BrBox *box=view_hit(&v,&mx,&my,&root_view,18,72,cw-52,ch-102);active_view=v;focus=0;
    if(!box){if(ev==EV_PRESS){selected=-1;active_control=-1;}api->gui_dirty();return;}
    if(box->object>=0&&v->doc->object[box->object].kind==BR_CONTROL){int ci=v->doc->object[box->object].ref;BrControl *c=&v->doc->control[ci];int editable=c->type==BC_TEXT||c->type==BC_PASSWORD||c->type==BC_AREA;if(!editable){if(ev==EV_PRESS){if(c->type==BC_SELECT&&!c->multiple&&!c->disabled){control_focus(v,ci);select_view=v;select_ci=ci;select_start=0;select_serial=nav_serial;select_popup();}else activate_control(v,ci,(my-2)/20);}return;}if(ev==EV_PRESS&&!c->disabled)control_focus(v,ci);
        if(!c->disabled&&(c->type==BC_TEXT||c->type==BC_PASSWORD||c->type==BC_AREA)&&(ev==EV_PRESS||(ev==EV_DRAG&&active_control==ci))){int cols=br_max(1,(box->w-12)/8),pos=control_field.offset+br_max(0,(mx-5+4)/8);if(c->type==BC_AREA){int off=0,row=br_max(0,(my-4)/16);while(row--&&c->value[off])off=tw_wrap(c->value,off,cols);pos=off+br_max(0,(mx-5+4)/8);}control_field.caret=br_min(control_field.len,pos);if(ev==EV_PRESS)control_field.anchor=control_field.caret;}return;
    }
    if(ev==EV_DRAG)return;active_control=-1;int link=box->object>=0?v->doc->object[box->object].link:v->doc->runs?v->doc->run[br_run_at(v->doc,box->start+br_min((box->end-box->start)-1,mx/8))].link:0;selected=link-1;
    if(link){if(ev==EV_PRESS)follow();else say(v->doc->page.link[selected].url);}else say("Ready. Tab selects links and form controls.");
}
static void wheel(int i,int dz){(void)i;if(!busy&&!pending.data)scroll_by(-dz*(links_view?3:48));}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"Browser"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;
    api=k;ui_init(k,0);tw_grow=br_realloc;bc_init(&cache,bc_budget(k->mem_total_kb()),cache_alloc,cache_release);static const AppDesc d={.title="Browser",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .open=opened,.close=closed,.draw=draw,.key=key,.mouse=mouse,.wheel=wheel,.drop=dropped,.client_size=initial,.min_client=size,.live_draw=APP_INDEPENDENT};
    browser_type=k->register_app(&d);if(browser_type<0)return 1;
    return k->register_opener("html",html_opener)<0||k->register_opener("htm",html_opener)<0;
}
