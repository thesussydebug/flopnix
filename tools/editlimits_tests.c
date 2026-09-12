#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kapi.h"
static int fail_alloc, allocations, writes, checks, failures;
static const u8 *input;
static u32 input_size;
static void *allocate(u32 n) { if(fail_alloc)return 0;void *p=malloc(n);if(p)allocations++;return p; }
static void release(void *p) { if(p)allocations--;free(p); }
static void copystr(char *d,const char *s,int n) { if(n)snprintf(d,n,"%s",s); }
static void *copybytes(void *d,const void *s,u32 n) { return memcpy(d,s,n); }
static void *movebytes(void *d,const void *s,u32 n) { return memmove(d,s,n); }
static int readfile(const char *name,u8 *p,u32 cap) { (void)name;u32 n=input_size<cap?input_size:cap;memcpy(p,input,n);return (int)n; }
static int writefile(const char *name,const u8 *p,u32 n) { (void)name;(void)p;(void)n;writes++;return 0; }
static int writable(void) { return 1; }
static const Kapi test_api = {.kmalloc=allocate,.kfree=release,.strlcpy=copystr,.memcpy=copybytes,.memmove=movebytes,.strcmp=strcmp,.strcasecmp=_stricmp,.fs_read=readfile,.fat_read=readfile,.fs_write=writefile,.fat_write=writefile,.fat_writable=writable};
#include "../kexts/edit.c"
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
int main(void)
{
    api=&test_api;edcols=40;edrows=10;
    u8 *sample=malloc(FS_MAXFILE+2);for(int i=0;i<FS_MAXFILE+2;i++)sample[i]='a';input=sample;
    CHECK(FS_MAXFILE==131072);
    edit_new(0);CHECK(allocations==1&&eds[0].buf&&!eds[0].ro);
    input_size=FS_MAXFILE;edit_load(0,"large.txt");CHECK(E->len==FS_MAXFILE&&!E->ro&&E->buf[FS_MAXFILE]==0);
    save_write();CHECK(writes==1);
    ins('b');CHECK(E->len==FS_MAXFILE);
    E->cur=FS_MAXFILE;del_at(0);E->cur=E->len;ins('b');CHECK(E->len==FS_MAXFILE&&E->buf[FS_MAXFILE-1]=='b'&&E->buf[FS_MAXFILE]==0);
    input_size=FS_MAXFILE+1;edit_load(0,"large.txt");CHECK(E->len==FS_MAXFILE&&E->ro&&E->buf[FS_MAXFILE]==0);
    save_picked("a:large.txt",E);CHECK(writes==1&&E->ro);
    edit_new(1);ed_load_usb("/large.txt");CHECK(E->len==FS_MAXFILE&&E->ro&&E->src==1);
    save_picked("u:/LARGE.TXT",E);CHECK(writes==1&&E->ro);
    input_size=FS_MAXFILE;ed_load_usb("/large.txt");CHECK(E->len==FS_MAXFILE&&!E->ro);
    CHECK(eds[0].buf!=eds[1].buf&&allocations==2);
    fail_alloc=1;edit_new(2);CHECK(!E->buf&&E->len==0&&E->ro);
    edit_open_a(2,"large.txt",sample,FS_MAXFILE);CHECK(!E->buf&&E->len==0&&E->ro);
    save_write();CHECK(writes==1);
    CHECK(eds[0].len==FS_MAXFILE&&eds[1].len==FS_MAXFILE);
    fail_alloc=0;edit_new(2);CHECK(E->buf&&allocations==3);
    edit_open_a(2,"large.txt",sample,FS_MAXFILE+1);CHECK(E->len==FS_MAXFILE&&E->ro&&E->buf[FS_MAXFILE]==0);
    edit_close(0);edit_close(1);edit_close(2);CHECK(allocations==0);
    free(sample);printf("EDITOR LIMITS: %d checks, %d failures\n",checks,failures);return failures!=0;
}
