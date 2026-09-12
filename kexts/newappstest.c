/* Controls app tests on disposable floppy images. */
#include "kapi.h"
#include "nettext.h"
static const Kapi *api;
static int failures,received,reentrant;
static u16 http_port=18080,gopher_port=17070;
static void serial(const char *s){for(;*s;s++){for(int i=0;i<100000;i++)if(api->inb(0x3fd)&32)break;api->outb(0x3f8,(u8)*s);}}
static void check(int ok,const char *s){serial(ok?"APPTEST PASS ":"APPTEST FAIL ");serial(s);serial("\n");if(!ok)failures++;}
static int sink(const u8 *s,int n,void *p){(void)s;(void)p;received+=n;if(!reentrant){reentrant=1;check(api->net_http_get(0x0202000au,http_port,"10.0.2.2","/",0,0,100)==-5,"simultaneous TCP refused");}return 1;}
static int stop_sink(const u8 *s,int n,void *p){(void)s;(void)n;(void)p;return 0;}
static void command(const char *s)
{
    if(!api->strcmp(s,"imports")){check(api->kext_load("desktop/import.kx")==41,"unresolved import rejected before entry");}
    else if(!api->strncmp(s,"open ",5)){int t=api->app_find(s+5);check(t>=0&&api->win_open(t)>=0,"app opened");}
    else if(!api->strncmp(s,"focus ",6)){int t=api->app_find(s+6);if(t>=0)api->win_focus(t,0);}
    else if(!api->strncmp(s,"close ",6)){int t=api->app_find(s+6);if(t>=0)api->win_close_self(t,0);}
    else if(!api->strncmp(s,"read ",5)){int n=api->fs_read(s+5,api->iobuf,api->iobuf_size);check(n>=0&&api->open_with(s+5,0,api->iobuf,n)>=0,"loaded file opener");}
    else if(!api->strncmp(s,"folder ",7)){check(!api->kext_load("sys/files.kx"),"Files loaded");api->broadcast("folder.open",s+7);}
    else if(!api->strcmp(s,"folders")){
        const u8 bytes[]={1,2,3,255};u8 read[4];
        check(!api->fs_mkdir("check"),"folder created");
        check(!api->fs_mkdir("check/sub"),"nested folder created");
        check(!api->fs_write("check/sub/a",bytes,4),"nested file written");
        check(!api->fs_rename_dir("check","renamed"),"whole folder renamed");
        check(api->fs_read("renamed/sub/a",read,4)==4&&read[0]==1&&read[1]==2&&read[2]==3&&read[3]==255,"nested bytes preserved");
        check(api->fs_rename_dir("renamed","renamed/sub/x")<0,"folder cycle refused");
        check(api->fs_rename_dir("renamed","abcdefghijklmnopqrstu")<0,"long child path refused");
        check(api->fs_exists("renamed/sub/a"),"failed rename preserves files");
        check(!api->fs_write("taken/other",bytes,4),"implicit folder fixture");
        check(api->fs_rename_dir("renamed","taken")<0,"implicit target folder refused");
        api->fs_delete("renamed/sub/a");api->fs_delete("renamed/sub");api->fs_delete("renamed");api->fs_delete("taken/other");
    }
    else if(!api->strncmp(s,"file ",5))check(api->open_with(s+5,0,0,0)>=0,"file opener");
    else if(!api->strncmp(s,"clip ",5)){char b[100];api->clip_get_text(b,sizeof b);check(!api->strcmp(b,s+5),"clipboard text");serial("CLIP: ");serial(b);serial("\n");}
    else if(!api->strncmp(s,"net ",4)){
        const char *arg=s+4;http_port=(u16)api->atoi(arg);while(*arg&&*arg!=' ')arg++;gopher_port=(u16)api->atoi(arg);
        u32 ip=api->net_dns("10.0.2.2",300);check(ip==0x0202000au,"numeric IPv4");received=reentrant=0;
        int result=api->net_http_get(ip,http_port,"10.0.2.2","/large",sink,0,500);
        char line[80];api->kfmt(line,sizeof line,"HTTP result %d; received %d bytes\n",result,received);serial(line);
        check(result==200&&received==18000,"streamed HTTP complete");
        check(api->net_http_get(ip,http_port,"10.0.2.2","/short",sink,0,500)==-4,"short HTTP refused");
        check(api->net_http_get(ip,http_port,"10.0.2.2","/large",stop_sink,0,500)==-4,"sink stop reported");
        const NetTextOps *net=api->service_get("net.text");check(net&&net->abi==NET_TEXT_ABI,"Gopher service available");received=0;
        if(net)check(net->request(ip,gopher_port,"/doc\r\n",sink,0,500)==0&&received>10,"Gopher transfer complete");
        serial("APPTEST NET DONE\n");
    }else if(!api->strcmp(s,"finish")){char b[80];check(api->fault_count()==0,"no recovered faults");api->kfmt(b,sizeof b,"APPTEST DONE: %d failures; %u heap bytes free\n",failures,api->heap_avail());serial(b);}
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"App Tests"};
int kext_entry(const Kapi *k){api=k;k->register_cmd("apptest","apptest - integration tests",command);serial("APPTEST READY\n");return 0;}
