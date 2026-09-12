#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "kapi.h"
static int checks,failures,allocations,fail_alloc,reads,notifications,hook;
static char *changing_payload;
static char usb_dir[180]="/docs/nested";
static FsEnt fixture[FS_NFILES];
static const u8 contents[]={0,1,2,3,255,10,32,65};
static void during_read(void);
static void *allocate(u32 n){if(fail_alloc)return 0;void *p=malloc(n);if(p)allocations++;return p;}
static void release(void *p){if(p)allocations--;free(p);}
static void copystr(char *d,const char *s,int cap){if(cap)snprintf(d,cap,"%s",s);}
static u32 stringlen(const char *s){return (u32)strlen(s);}
static void *copybytes(void *d,const void *s,u32 n){return memcpy(d,s,n);}
static void *movebytes(void *d,const void *s,u32 n){return memmove(d,s,n);}
static void *fillbytes(void *d,int c,u32 n){return memset(d,c,n);}
static void format(char *d,int cap,const char *f,...){va_list ap;va_start(ap,f);vsnprintf(d,cap,f,ap);va_end(ap);}
static void nop(void){}
static void busy(const char *a,const char *b,int c){(void)a;(void)b;(void)c;}
static void notify_text(const char *s){(void)s;notifications++;}
static u32 total(void){return 8064;}
static u32 info(int kind){(void)kind;return 1048576;}
static FsEnt *slot(int i){return &fixture[i];}
static int read_local(const char *path,u8 *out,u32 cap){
    reads++;during_read();
    for(int i=0;i<FS_NFILES;i++)if(fixture[i].used&&!strcmp(path,fixture[i].name)){
        u32 n=fixture[i].size<cap?fixture[i].size:cap;for(u32 j=0;j<n;j++)out[j]=contents[j%sizeof contents];return n;
    }
    return -1;
}
static int list_usb(const char *dir,FatEnt *out,int cap){
    if(strcmp(dir,usb_dir)||cap<1)return -1;
    memset(out,0,sizeof *out);copystr(out->name,"usb.txt",sizeof out->name);out->size=sizeof contents;return 1;
}
static int read_usb(const char *path,u8 *out,u32 cap){
    char expected[200];snprintf(expected,sizeof expected,"%s/usb.txt",usb_dir);
    reads++;during_read();if(strcmp(path,expected)||cap<sizeof contents)return -1;
    memcpy(out,contents,sizeof contents);return sizeof contents;
}
static const Kapi test_api={.kmalloc=allocate,.kfree=release,.strlcpy=copystr,.strcmp=strcmp,.strcasecmp=_stricmp,.strlen=stringlen,
    .memcpy=copybytes,.memmove=movebytes,.memset=fillbytes,.kfmt=format,.gui_dirty=nop,.busy_set=busy,.busy_end=nop,
    .notify=notify_text,.mem_total_kb=total,.mem_info=info,.fs_slot=slot,.fs_read=read_local,.fat_read=read_usb,.fat_list=list_usb};
#include "../kexts/archive.c"
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void during_read(void){
    int h=hook;hook=0;
    if(h==1){memset(changing_payload,'x',strlen(changing_payload));}
    if(h==2){dropped(0,0,0,"file","a:other/two.txt");key(0,K_DEL);action(0);closed(0);}
}
static void reset(void){dirty=0;working=0;closed(0);opened(0);}
static void add_fixture(int i,const char *name,u32 size){fixture[i].used=1;copystr(fixture[i].name,name,sizeof fixture[i].name);fixture[i].size=size;}
int main(void){
    api=&test_api;
    add_fixture(0,"desktop/one.txt",sizeof contents);add_fixture(1,"other/two.txt",17);
    add_fixture(2,"deep/sub/three.txt",4);add_fixture(3,"deep/big.txt",AR_FILE+1);
    add_fixture(4,"deep/max.txt",AR_FILE);opened(0);
    dropped(0,0,0,"text","a:desktop/one.txt");CHECK(count==0&&reads==0);
    dropped(0,0,0,"file","a:desktop/one.txt");CHECK(count==1&&dirty&&!working&&reads==1);
    u8 raw[32];CHECK(lz_unpack(data+entries[0].offset,entries[0].packed,raw,sizeof raw)==sizeof contents);
    CHECK(!memcmp(raw,contents,sizeof contents));CHECK(!strcmp(entries[0].name,"one.txt"));
    dropped(0,0,0,"file","a:desktop/one.txt\na:deep/big.txt\na:other/two.txt\na:deep/sub/three.txt");
    CHECK(count==3&&strstr(message,"2 added; 2 skipped"));CHECK(!strcmp(entries[2].name,"three.txt"));CHECK(reads==3);
    dropped(0,0,0,"file","u:/docs/nested/usb.txt");CHECK(count==4&&reads==4);
    CHECK(lz_unpack(data+entries[3].offset,entries[3].packed,raw,sizeof raw)==sizeof contents);
    CHECK(!memcmp(raw,contents,sizeof contents));
    dropped(0,0,0,"file","a:deep/sub/");CHECK(count==4&&strstr(message,"Open the folder"));
    dropped(0,0,0,"file","z:bad.txt");CHECK(count==4&&strstr(message,"not supported"));
    char longpath[240];memset(longpath,'a',sizeof longpath);longpath[239]=0;
    dropped(0,0,0,"file",longpath);CHECK(count==4&&strstr(message,"too long"));
    char huge[4096];memset(huge,'a',sizeof huge);dropped(0,0,0,"file",huge);CHECK(count==4&&strstr(message,"fewer files"));
    reset();char changing[]="a:desktop/one.txt\na:other/two.txt";changing_payload=changing;hook=1;
    dropped(0,0,0,"file",changing);CHECK(count==2&&!strcmp(entries[1].name,"two.txt"));
    reset();hook=2;dropped(0,0,0,"file","a:desktop/one.txt\na:other/two.txt");
    CHECK(count==1&&dirty&&!working&&!alive&&notifications==1);CHECK(data!=0);
    reset();fail_alloc=1;dropped(0,0,0,"file","a:desktop/one.txt");CHECK(count==0&&!working&&!data);fail_alloc=0;
    dropped(0,0,0,"file","a:missing.txt\na:deep/max.txt");CHECK(count==1&&entries[0].raw==AR_FILE&&strstr(message,"1 added; 1 skipped"));
    reset();usb_dir[0]=0;for(int i=0;i<18;i++)strcat(usb_dir,"/nested");
    char deep[200];snprintf(deep,sizeof deep,"u:%s/usb.txt",usb_dir);dropped(0,0,0,"file",deep);
    CHECK(count==1&&entries[0].raw==sizeof contents);
    reset();closed(0);CHECK(allocations==0);
    printf("ARCHIVE DROPS: %d checks, %d failures\n",checks,failures);return failures!=0;
}
