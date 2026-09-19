#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../kexts/textweb.c"

static int passed,failed,allocations,fail_alloc,stop_read,close_read,picker_calls;
static int (*openers[2])(const char *,const char *,const u8 *,int);
static const AppDesc *desc;
static const char first[]="<style>p { color: red; }</style><p>First local page</p><a href='next.htm'>Next</a>";
static const char second[]="<h1>Second local page</h1><a href='../start.html'>Start</a>";
static u32 fake_length;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %d: %s\n",__LINE__,#x);}} while(0)
static void nothing(void){}
static int stopped(void){return stop_read;}
static u32 memory(void){return 4096;}
static void track(const char *s,const void *p,u32 n){(void)s;(void)p;(void)n;}
static void *alloc(u32 n){if(fail_alloc)return 0;void *p=malloc(n);if(p)allocations++;return p;}
static void release_alloc(void *p){if(p)allocations--;free(p);}
static void *resize(void *p,u32 n){if(fail_alloc)return 0;return realloc(p,n);}
static void copy_string(char *d,const char *s,int cap){snprintf(d,(size_t)cap,"%s",s);}
static u32 string_length(const char *s){return (u32)strlen(s);}
static void *copy_memory(void *d,const void *s,u32 n){return memcpy(d,s,n);}
static void *move_memory(void *d,const void *s,u32 n){return memmove(d,s,n);}
static void *set_memory(void *d,int c,u32 n){return memset(d,c,n);}
static void format(char *d,int cap,const char *f,...){va_list args;va_start(args,f);vsnprintf(d,(size_t)cap,f,args);va_end(args);}
static int read_file(const char *s,u8 *buf,u32 cap)
{
    if(close_read)closed(0);
    if(!strcmp(s,"large.htm")){
        u32 n=fake_length<cap?fake_length:cap;memset(buf,' ',n);
        if(n>=22)memcpy(buf,"<p>Large local page</p>",22);return (int)n;
    }
    const char *text=!strcmp(s,"docs/start.html")||!strcmp(s,"/docs/start.html")?first:
                     !strcmp(s,"docs/next.htm")||!strcmp(s,"/docs/next.htm")?second:0;
    if(!text)return -1;u32 n=(u32)strlen(text);if(n>cap)n=cap;memcpy(buf,text,n);return (int)n;
}
static int register_app(const AppDesc *d){desc=d;return 9;}
static int register_open(const char *ext,int (*fn)(const char *,const char *,const u8 *,int))
{
    if(!strcmp(ext,"html"))openers[0]=fn;else if(!strcmp(ext,"htm"))openers[1]=fn;else return -1;return 0;
}
static int open_window(int type){CHECK(type==9);desc->open(0);return 0;}
static void picker(const char *title,const char *ext,int dirs,void (*cb)(const char *,void *),void *ctx)
{
    (void)title;(void)ext;CHECK(!dirs);picker_calls++;cb("u:/docs/start.html",ctx);
}
int main(void)
{
    Kapi mock={.version=KAPI_VERSION,.register_app=register_app,.register_opener=register_open,.win_open=open_window,
        .gui_dirty=nothing,.esc_arm=nothing,.esc_pending=stopped,.mem_total_kb=memory,.mem_track=track,
        .kmalloc=alloc,.krealloc=resize,.kfree=release_alloc,.strlcpy=copy_string,.strlen=string_length,
        .strcmp=strcmp,.memcpy=copy_memory,.memmove=move_memory,.memset=set_memory,.kfmt=format,
        .fs_read=read_file,.fat_read=read_file,.file_picker=picker};
    CHECK(!kext_entry(&mock));CHECK(openers[0]&&openers[1]);
    CHECK(!openers[0]("docs/start.html",0,(const u8 *)"truncated",9));
    CHECK(page&&strstr(page->page.text,"First local page")&&page->page.links==1);
    CHECK(!strcmp(current,"a:docs/start.html")&&!gopher&&!at_home);
    selected=0;follow();CHECK(!strcmp(current,"a:docs/next.htm")&&strstr(page->page.text,"Second local page"));
    key(0,K_LEFT);CHECK(!strcmp(current,"a:docs/start.html"));
    key(0,K_RIGHT);CHECK(!strcmp(current,"a:docs/next.htm"));
    BrPage *previous=page;int old_hpos=hpos;
    visit("a:missing.htm",-1,0);CHECK(page==previous&&hpos==old_hpos&&strstr(status,"Could not read"));
    fail_alloc=1;visit("a:docs/start.html",-1,0);fail_alloc=0;CHECK(page==previous&&hpos==old_hpos);
    stop_read=1;visit("a:docs/start.html",-1,0);stop_read=0;CHECK(page==previous&&hpos==old_hpos);
    fake_length=FS_MAXFILE+1;visit("a:large.htm",-1,0);CHECK(page==previous&&strstr(status,"exceeds"));
    fake_length=FS_MAXFILE;visit("a:large.htm",-1,0);CHECK(strstr(page->page.text,"Large local page")&&strstr(status,"524288 bytes"));
    CHECK(!openers[1]("start.html","/docs/start.html",0,0));CHECK(!strcmp(current,"u:/docs/start.html"));
    selected=0;follow();CHECK(!strcmp(current,"u:/docs/next.htm"));
    key(0,15);CHECK(picker_calls==1&&!strcmp(current,"u:/docs/start.html"));
    dropped(0,0,0,"file","a:docs/next.htm");CHECK(!strcmp(current,"a:docs/next.htm"));
    dropped(0,0,0,"file","a:docs/start.html\na:docs/next.htm");CHECK(!strcmp(current,"a:docs/next.htm")&&strstr(status,"one HTML"));
    for(int i=0;i<12;i++)visit(i&1?"a:docs/start.html":"a:docs/next.htm",-1,0);
    CHECK(hcount==8&&hpos==7&&allocations==1);
    close_read=1;visit("a:docs/start.html",-1,0);close_read=0;CHECK(!page&&!alive&&!busy&&!closing&&allocations==0);
    open_window(9);CHECK(page&&alive);closed(0);CHECK(allocations==0);
    printf("BROWSER: %d pass %d fail\n",passed,failed);return failed!=0;
}
