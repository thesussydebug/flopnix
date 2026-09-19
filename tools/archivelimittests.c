#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../kexts/archive.c"

static int passed,failed,live_allocs,fail_after=-1,short_read,writes;
static u32 heap_room=32*1024*1024;
static FsEnt files[FS_NFILES];
static u8 *contents[FS_NFILES];
static u8 *usb_data;
static u32 usb_size;
#define CHECK(x) do {if(x)passed++;else {failed++;printf("FAIL %d: %s\n",__LINE__,#x);}} while(0)
static void nothing(void){}
static int no(void){return 0;}
static int yes(void){return 1;}
static int no_dir(const char *p){(void)p;return 0;}
static u32 space(void){return 32768;}
static u32 info(int which){(void)which;return heap_room;}
static void track(const char *s,const void *p,u32 n){(void)s;(void)p;(void)n;}
static int fail_alloc(void){if(fail_after<0)return 0;if(!fail_after)return 1;fail_after--;return 0;}
static void *alloc(u32 n){if(fail_alloc())return 0;void *p=malloc(n);if(p)live_allocs++;return p;}
static void *resize(void *p,u32 n){if(fail_alloc())return 0;void *q=realloc(p,n);if(q&&!p)live_allocs++;return q;}
static void release(void *p){if(p)live_allocs--;free(p);}
static void copystr(char *d,const char *s,int n){snprintf(d,(size_t)n,"%s",s);}
static u32 slen(const char *s){return (u32)strlen(s);}
static void *cp(void *d,const void *s,u32 n){return memcpy(d,s,n);}
static void *mv(void *d,const void *s,u32 n){return memmove(d,s,n);}
static void *zero(void *d,int c,u32 n){return memset(d,c,n);}
static void fmt(char *d,int n,const char *f,...){va_list a;va_start(a,f);vsnprintf(d,(size_t)n,f,a);va_end(a);}
static void event(const char *e,const char *v){(void)e;(void)v;}
static void busy(const char *a,const char *b,int n){(void)a;(void)b;(void)n;}
static FsEnt *slot(int i){return &files[i];}
static int findfile(const char *p){for(int i=0;i<FS_NFILES;i++)if(files[i].used&&!strcmp(files[i].name,p))return i;return -1;}
static int exists(const char *p){return findfile(p)>=0;}
static int readfile(const char *p,u8 *d,u32 cap){int i=findfile(p);if(i<0)return -1;u32 n=files[i].size;if(n>cap)n=cap;if(short_read&&n)n--;memcpy(d,contents[i],n);return (int)n;}
static int writefile(const char *p,const u8 *d,u32 n){int i=findfile(p);if(i<0)for(i=0;i<FS_NFILES&&files[i].used;i++);if(i==FS_NFILES)return -2;free(contents[i]);contents[i]=malloc(n?n:1);memcpy(contents[i],d,n);files[i].used=1;files[i].size=n;copystr(files[i].name,p,24);writes++;return 0;}
static int usb_read(const char *p,u8 *d,u32 cap){if(strcmp(p,"/usb.bin"))return -1;u32 n=usb_size<cap?usb_size:cap;memcpy(d,usb_data,n);return (int)n;}
static int usb_list(const char *p,FatEnt *e,int cap){if(strcmp(p,"/")||!cap)return -1;memset(e,0,sizeof *e);copystr(e->name,"usb.bin",64);e->size=usb_size;return 1;}
static int usb_exists(const char *p){(void)p;return 0;}
static int usb_write(const char *p,const u8 *d,u32 n){CHECK(strstr(p,".bin")!=0);CHECK(n==usb_size&&!memcmp(d,usb_data,n));writes++;return 0;}
static Kapi mock={.kmalloc=alloc,.krealloc=resize,.kfree=release,.mem_info=info,.mem_track=track,
    .strlcpy=copystr,.strlen=slen,.strcmp=strcmp,.strcasecmp=_stricmp,.memcpy=cp,.memmove=mv,.memset=zero,.kfmt=fmt,
    .gui_dirty=nothing,.busy_set=busy,.busy_end=nothing,.broadcast=event,.esc_arm=nothing,.esc_pending=no,
    .fs_slot=slot,.fs_read=readfile,.fs_write=writefile,.fs_exists=exists,.fs_dir_count=no_dir,.fs_free_kb=space,
    .fat_read=usb_read,.fat_list=usb_list,.fat_exists=usb_exists,.fat_write=usb_write,.fat_writable=yes,.usb_present=yes};
static void same_archive(const u8 *old,u32 n,int c){CHECK(length==n&&count==c&&!memcmp(data,old,n));CHECK(ar_index(data,length,entries)==count);}
int main(void)
{
    api=&mock;alive=1;clear();
    u32 sizes[]={131073,524288,524289,2*1024*1024};
    for(int j=0;j<4;j++){
        u32 n=sizes[j];u8 *raw=malloc(n);memset(raw,'A'+j,n);writefile("source.bin",raw,n);
        clear();add_path("a:source.bin",0);CHECK(count==1&&entries[0].raw==n&&dirty);
        u8 *out=malloc(n);CHECK(lz_unpack(data+entries[0].offset,entries[0].packed,out,n)==(int)n&&!memcmp(raw,out,n));free(out);
        save_now(MBR_YES,0);CHECK(!dirty&&exists("desktop/files.fpa"));clear();load_path("a:desktop/files.fpa");CHECK(count==1&&entries[0].raw==n&&!dirty);
        extract_all=1;int before=writes;extract_to("a:dest",0);CHECK(writes==before+1);int dest=findfile("dest/source.bin");CHECK(dest>=0&&files[dest].size==n&&!memcmp(contents[dest],raw,n));files[dest].used=0;
        CHECK(files[findfile("source.bin")].size==n&&!memcmp(contents[findfile("source.bin")],raw,n));free(raw);
    }
    usb_size=1152*1024;usb_data=malloc(usb_size);u32 rng=1;
    for(u32 i=0;i<usb_size;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;usb_data[i]=(u8)rng;}
    clear();dropped(0,0,0,"file","u:/usb.bin");CHECK(count==1&&entries[0].raw==usb_size&&length>1048576);
    save_now(MBR_YES,0);clear();load_path("a:desktop/files.fpa");CHECK(count==1&&entries[0].raw==usb_size&&length>1048576);
    int before=writes;extract_all=1;extract_to("u:/out",0);CHECK(writes==before+1);
    u32 old_length=length;int old_count=count;u8 *old=malloc(length);memcpy(old,data,length);
    fail_after=0;add_path("a:source.bin",0);same_archive(old,old_length,old_count);fail_after=-1;
    fail_after=1;heap_room=0;add_path("a:source.bin",0);same_archive(old,old_length,old_count);fail_after=-1;heap_room=32*1024*1024;
    short_read=1;add_path("a:source.bin",0);same_archive(old,old_length,old_count);short_read=0;
    fail_after=0;load_path("a:desktop/files.fpa");same_archive(old,old_length,old_count);fail_after=-1;
    fail_after=1;load_path("a:desktop/files.fpa");same_archive(old,old_length,old_count);fail_after=-1;
    int saved=findfile("desktop/files.fpa");contents[saved][44]^=1;load_path("a:desktop/files.fpa");same_archive(old,old_length,old_count);contents[saved][44]^=1;
    u32 raw_size=entries[0].raw;lz_put32(data+entries[0].offset+4,0xffffffffu);CHECK(ar_index(data,length,entries)<0);lz_put32(data+36+4,raw_size);CHECK(ar_index(data,length,entries)==1);
    files[findfile("source.bin")].size=0xffffffffu;add_path("a:source.bin",0);same_archive(old,old_length,old_count);
    free(old);free(usb_data);clear();CHECK(live_allocs==0);
    for(int i=0;i<FS_NFILES;i++)free(contents[i]);
    printf("ARCHIVE LIMITS: %d pass %d fail\n",passed,failed);return failed!=0;
}
