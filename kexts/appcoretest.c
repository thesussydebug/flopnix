/* Runs the app logic tests in a low-memory guest. */
#include "kapi.h"
static const Kapi *api;
static int pass,fail;
static void serial(const char *s){while(*s){for(int i=0;i<100000;i++)if(api->inb(0x3fd)&32)break;api->outb(0x3f8,(u8)*s++);}}
static void check(int ok,const char *s){if(ok)pass++;else{fail++;serial("FAIL ");serial(s);serial("\n");}}
#define CHECK(c) check((c),#c)
#define APP_TEST_PHASE(s) serial("APP CORE: " s "\n")
#include "newapps_tests.inc"
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"App Core Tests"};
int kext_entry(const Kapi *k){api=k;serial("APP CORES START\n");t_newapps();char b[80];api->kfmt(b,sizeof b,"APP CORES DONE: %d pass %d fail\n",pass,fail);serial(b);return fail!=0;}
