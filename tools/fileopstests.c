/* Tests file transfers with simulated disks and I/O failures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "kapi.h"
#include "fspath.inc"
#include "shpath.h"
#include "../kexts/foldercopy.inc"
#include "../kexts/fileops.inc"
typedef struct {int used,drive;char path[96];u32 size;u8 *bytes;} Entry;
static Entry disk[128];static FsEnt slots[128];static int fails,checks;
static int fail_write,fail_delete,grow;static u32 memory=4000000;
static char cliptype[16],clip[4096];static int cliplen;
#define CHECK(x) do{checks++;if(!(x)){fails++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static void *cp(void *d,const void *s,u32 n){return memcpy(d,s,n);}
static void *zero(void *d,int c,u32 n){return memset(d,c,n);}
static u32 length(const char *s){return (u32)strlen(s);}
static void copystr(char *d,const char *s,int n){if(n){snprintf(d,n,"%s",s);}}
static void fmt(char *d,int n,const char *f,...){va_list a;va_start(a,f);vsnprintf(d,n,f,a);va_end(a);}
static Entry *find(int drive,const char *p){for(int i=0;i<128;i++)if(disk[i].used&&disk[i].drive==drive&&!(drive?_stricmp(disk[i].path,p):strcmp(disk[i].path,p)))return &disk[i];return 0;}
static int writefile(int drive,const char *p,const u8 *data,u32 n){if(fail_write)return -1;Entry *e=find(drive,p);if(!e)for(int i=0;i<128;i++)if(!disk[i].used){e=&disk[i];break;}if(!e)return -2;
    free(e->bytes);e->bytes=malloc(n?n:1);memcpy(e->bytes,data,n);e->used=1;e->drive=drive;e->size=n;copystr(e->path,p,96);return 0;}
static int fw(const char *p,const u8 *b,u32 n){return writefile(0,p,b,n);}
static int uw(const char *p,const u8 *b,u32 n){return writefile(1,p,b,n);}
static int readfile(int drive,const char *p,u8 *b,u32 cap){Entry *e=find(drive,p);if(!e)return -1;u32 n=e->size<cap?e->size:cap;memcpy(b,e->bytes,n);if(grow&&n<cap)b[n++]=0;return n;}
static int fr(const char *p,u8 *b,u32 n){return readfile(0,p,b,n);}
static int ur(const char *p,u8 *b,u32 n){return readfile(1,p,b,n);}
static int del(int drive,const char *p){Entry *e=find(drive,p);if(!e||fail_delete)return -1;e->used=0;free(e->bytes);e->bytes=0;return 0;}
static int fd(const char *p){return del(0,p);}static int ud(const char *p){return del(1,p);}
static int fe(const char *p){return find(0,p)!=0;}static int ue(const char *p){return find(1,p)!=0;}
static int no_dir(const char *p){(void)p;return 0;}
static int dircount(const char *p){int n=0;for(int i=0;i<128;i++)if(disk[i].used&&!disk[i].drive&&fs_under(disk[i].path,p))n++;return n;}
static int renamefile(const char *a,const char *b){Entry *e=find(0,a);if(!e||fe(b)||strlen(b)>=24)return -1;copystr(e->path,b,96);return 0;}
static int renamedir(const char *a,const char *b){(void)a;(void)b;return -1;}
static int mkdirfake(const char *p){(void)p;return 0;}
static FsEnt *slot(int i){Entry *e=&disk[i];memset(&slots[i],0,sizeof slots[i]);if(e->used&&!e->drive){slots[i].used=1;copystr(slots[i].name,e->path,24);slots[i].size=e->size;}return &slots[i];}
static int ulist(const char *dir,FatEnt *out,int cap){int n=0;for(int i=0;i<128&&n<cap;i++)if(disk[i].used&&disk[i].drive){const char *base=ft_leaf(disk[i].path);int d=(int)(base-disk[i].path);if(d>1)d--;
    if((int)strlen(dir)==d&&!strncmp(dir,disk[i].path,d)){memset(&out[n],0,sizeof *out);copystr(out[n].name,base,64);out[n++].size=disk[i].size;}}
    return n;}
static int yes(void){return 1;}static u32 heap(void){return memory;}static u32 space(void){return 4096;}
static void *alloc(u32 n){return malloc(n);}static void release(void *p){free(p);}
static void nothing(void){}static void event(const char *e,const char *v){(void)e;(void)v;}
static void busy(const char *t,const char *m,int n){(void)t;(void)m;(void)n;}
static int cs(const char *t,const void *p,u32 n){copystr(cliptype,t,16);if(n>sizeof clip)return -1;memcpy(clip,p,n);cliplen=n;return 0;}
static int cg(const char *t,void *p,u32 n){if(strcmp(t,cliptype))return 0;u32 count=cliplen<(int)n?cliplen:n;memcpy(p,clip,count);return count;}
static int cmpn(const char *a,const char *b,u32 n){return strncmp(a,b,n);}
static Kapi k={.strlen=length,.strlcpy=copystr,.strcmp=strcmp,.strcasecmp=_stricmp,.strncmp=cmpn,.memcpy=cp,.memset=zero,.kfmt=fmt,
    .fs_slot=slot,.fs_read=fr,.fs_write=fw,.fs_delete=fd,.fs_exists=fe,.fs_is_dir=no_dir,.fs_dir_count=dircount,.fs_rename=renamefile,.fs_rename_dir=renamedir,.fs_mkdir=mkdirfake,.fs_free_kb=space,
    .fat_read=ur,.fat_write=uw,.fat_delete=ud,.fat_exists=ue,.fat_list=ulist,.fat_mkdir=mkdirfake,.fat_writable=yes,.usb_present=yes,
    .heap_avail=heap,.kmalloc=alloc,.kfree=release,.busy_set=busy,.busy_end=nothing,.broadcast=event,.gui_dirty=nothing,.clip_get=cg,.clip_set=cs};
static int equal(int drive,const char *p,const u8 *b,int n){Entry *e=find(drive,p);return e&&e->size==(u32)n&&!memcmp(e->bytes,b,n);}
int main(void)
{
    const u8 bytes[]={0,1,255,3};char dst[96];int moved;FtBatch b;
    fw("in/a.fpa",bytes,4);CHECK(ft_transfer(&k,"a:in/a.fpa",1,"/",FT_COPY,dst,&moved)==0);
    CHECK(!strcmp(dst,"/a.fpa")&&equal(1,dst,bytes,4)&&fe("in/a.fpa"));
    CHECK(ft_transfer(&k,"a:in/a.fpa",1,"/",FT_MOVE,dst,&moved)==FT_EXISTS&&fe("in/a.fpa"));
    uw("/one/b.fpa",bytes,4);CHECK(ft_transfer(&k,"u:/one/b.fpa",1,"/two",FT_DRAG,dst,&moved)==0&&moved);
    CHECK(!ue("/one/b.fpa")&&equal(1,"/two/b.fpa",bytes,4));
    fw("in/one.txt",bytes,4);fw("in/two.txt",bytes,4);fw("desktop/two.txt",bytes,4);
    const char *list="a:in/one.txt\na:in/two.txt";cs("file.cut",list,strlen(list)+1);
    ft_batch(&k,list,0,"desktop",FT_MOVE,&b);CHECK(b.moved==1&&b.failed==1&&!fe("in/one.txt")&&fe("in/two.txt"));
    ft_clip_finish(&k,list,&b);CHECK(!strcmp(clip,"a:in/two.txt"));
    char msg[64],clipread[4096];int mode;ft_message(&k,&b,msg,sizeof msg);CHECK(strstr(msg,"1 of 2 done")&&strstr(msg,"Name already exists"));
    CHECK(ft_clip_get(&k,clipread,&mode)&&mode==FT_MOVE&&!strcmp(clipread,"a:in/two.txt"));
    cs("file","a:new.txt",10);ft_clip_finish(&k,list,&b);CHECK(!strcmp(cliptype,"file")&&!strcmp(clip,"a:new.txt"));
    u8 *large=malloc(98304);for(int i=0;i<98304;i++)large[i]=(u8)(i*73);fw("large.fpa",large,98304);
    k.iobuf_size=32768;CHECK(ft_transfer(&k,"a:large.fpa",1,"/",FT_COPY,dst,&moved)==0&&equal(1,dst,large,98304));
    memory=100;CHECK(ft_transfer(&k,"a:large.fpa",1,"/low",FT_MOVE,dst,&moved)==FT_MEMORY&&fe("large.fpa"));memory=4000000;
    grow=1;CHECK(ft_transfer(&k,"a:in/a.fpa",1,"/g",FT_COPY,dst,&moved)==FT_READ&&!ue("/g/a.fpa"));grow=0;
    fail_write=1;CHECK(ft_transfer(&k,"a:in/a.fpa",1,"/w",FT_MOVE,dst,&moved)==FT_WRITE&&fe("in/a.fpa"));fail_write=0;
    fail_delete=1;CHECK(ft_transfer(&k,"a:in/a.fpa",1,"/d",FT_MOVE,dst,&moved)==FT_DELETE&&fe("in/a.fpa")&&equal(1,dst,bytes,4));fail_delete=0;
    fw("empty.fpa",bytes,0);CHECK(ft_transfer(&k,"a:empty.fpa",1,"/",FT_COPY,dst,&moved)==0&&equal(1,dst,bytes,0));
    fw("package-one.fpa",bytes,4);fw("package-two.fpa",large,6);
    CHECK(ft_transfer(&k,"a:package-one.fpa",1,"/",FT_COPY,dst,&moved)==0&&!strcmp(dst,"/PACKAG~1.FPA"));
    CHECK(ft_transfer(&k,"a:package-two.fpa",1,"/",FT_COPY,dst,&moved)==0&&!strcmp(dst,"/PACKAG~2.FPA"));
    CHECK(equal(1,"/PACKAG~1.FPA",bytes,4)&&equal(1,"/PACKAG~2.FPA",large,6));
    CHECK(ft_join(&k,0,"desktop","12345678901.txt",dst)==0&&strlen(dst)==23);
    CHECK(ft_join(&k,0,"desktop","123456789012.txt",dst)==FT_PATH);
    CHECK(ft_join(&k,0,"desktop","../a",dst)==FT_PATH);
    CHECK(ft_transfer(&k,"a:in/a.fpa",0,"in",FT_COPY,dst,&moved)==0&&!strcmp(dst,"in/a_1.fpa")&&fe("in/a.fpa"));
    for(int i=2;i<=9;i++){char name[24];snprintf(name,sizeof name,"in/a_%d.fpa",i);fw(name,large,6);}
    CHECK(ft_transfer(&k,"a:in/a.fpa",0,"in",FT_COPY,dst,&moved)==0&&!strcmp(dst,"in/a_10.fpa"));
    CHECK(equal(0,"in/a_9.fpa",large,6)&&equal(0,"in/a_10.fpa",bytes,4));
    CHECK(ft_transfer(&k,"a:in/a.fpa",0,"in",FT_DRAG,dst,&moved)==1&&fe("in/a.fpa"));
    fw("sys/a.kx",bytes,4);CHECK(ft_transfer(&k,"a:sys/a.kx",0,"desktop",FT_DRAG,dst,&moved)==0&&!moved&&fe("sys/a.kx"));
    CHECK(ft_transfer(&k,"a:sys/a.kx",0,"other",FT_MOVE,dst,&moved)==FT_SYSTEM);
    fw("tree/sub/a.txt",bytes,4);CHECK(ft_transfer(&k,"a:tree/",1,"/",FT_COPY,dst,&moved)==0&&equal(1,"/tree/sub/a.txt",bytes,4));
    CHECK(ft_transfer(&k,"u:/two/",0,"",FT_COPY,dst,&moved)==0&&equal(0,"two/b.fpa",bytes,4));
    CHECK(ft_transfer(&k,"u:/two/",1,"/TWO/child",FT_COPY,dst,&moved)==FT_PATH);
    CHECK(ft_transfer(&k,"a:missing/",1,"/",FT_COPY,dst,&moved)==FT_MISSING);
    char deep[96],spec[98];memset(deep,'d',63);deep[0]='/';strcpy(deep+63,"/deep.fpa");uw(deep,bytes,4);
    snprintf(spec,sizeof spec,"u:%s",deep);
    CHECK(ft_transfer(&k,spec,0,"desktop",FT_COPY,dst,&moved)==0&&equal(0,"desktop/deep.fpa",bytes,4));
    printf("FILE OPS: %d checks, %d failures\n",checks,fails);free(large);
    for(int i=0;i<128;i++)free(disk[i].bytes);return fails!=0;
}
