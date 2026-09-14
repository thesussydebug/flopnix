#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "kapi.h"
static int checks,failures,allocations,close_during_read;
static const AppDesc *desc;
static u8 bitmap[58],io[100];
static void *allocate(u32 n){void *p=malloc(n);if(p)allocations++;return p;}
static void release(void *p){if(p)allocations--;free(p);}
static void copystr(char *d,const char *s,int n){if(n)snprintf(d,n,"%s",s);}
static void *fill(void *d,int c,u32 n){return memset(d,c,n);}
static void format(char *d,int n,const char *f,...){va_list a;va_start(a,f);vsnprintf(d,n,f,a);va_end(a);}
static int readfile(const char *s,u8 *p,u32 n){(void)s;if(n<sizeof bitmap)return -1;if(close_during_read&&desc->close){int reopen=close_during_read==2;close_during_read=0;desc->close(0);if(reopen&&desc->open)desc->open(0);}memcpy(p,bitmap,sizeof bitmap);return sizeof bitmap;}
static u8 nearest(u8 r,u8 g,u8 b){(void)r;(void)g;(void)b;return 5;}
static const void *service(const char *s){(void)s;return 0;}
static int register_app(const AppDesc *d){desc=d;return 1;}
static void redraw(void){}
static const Kapi test_api={.version=KAPI_VERSION,.kmalloc=allocate,.kfree=release,.memset=fill,.strlcpy=copystr,
    .strcmp=strcmp,.kfmt=format,.fs_read=readfile,.iobuf=io,.iobuf_size=sizeof io,.palette_nearest=nearest,
    .service_get=service,.register_app=register_app,.gui_dirty=redraw};
#include "../kexts/settings.c"
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(void){
    bitmap[0]='B';bitmap[1]='M';bitmap[2]=58;bitmap[10]=54;bitmap[14]=40;bitmap[18]=bitmap[22]=bitmap[26]=1;bitmap[28]=24;
    CHECK(!kext_entry(&test_api));strcpy(dpath,"test.bmp");
    for(int i=0;i<20;i++){
        if(desc->open)desc->open(0);
        pv_load();CHECK(pv&&allocations==1);page=1;if(desc->close)desc->close(0);
        CHECK(!pv&&allocations==0&&page==0);
    }
    if(desc->open)desc->open(0);
    wp_picked("u:/test.bmp",0);CHECK(!strcmp(dpath,"test.bmp")&&!pv&&strstr(wmsg,"A:"));
    if(desc->open)desc->open(0);close_during_read=1;pv_load();CHECK(!pv&&allocations==0);
    wp_picked("other.bmp",0);CHECK(!strcmp(dpath,"test.bmp")&&!pv&&allocations==0);
    if(desc->open)desc->open(0);close_during_read=2;pv_load();CHECK(!pv&&allocations==0);
    pv_load();CHECK(pv&&allocations==1);if(desc->close)desc->close(0);CHECK(!pv&&allocations==0);
    if(pv){release(pv);pv=0;}
    printf("SETTINGS MEMORY: %d checks, %d failures\n",checks,failures);return failures!=0;
}
