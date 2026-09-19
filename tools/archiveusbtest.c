#define kext_entry archive_entry
#define kext_header archive_header
#include "../kexts/archive.c"
#undef kext_entry
#undef kext_header

static const Kapi *real_api;
static Kapi test_api;
static int test_timer,passes,failures,write_count,fail_after=-1,cancel_after=-1;
static u8 fixture[4096],result[4096];
static u32 fixture_size;
static const u8 payload[]="Archive USB round trip\n";
static void report(const char *s)
{
    while(*s){int guard=200000;while(!(api->inb(0x3fd)&0x20)&&--guard){}api->outb(0x3f8,(u8)*s++);}
}
static void check(int ok,const char *what)
{
    if(ok)passes++;else {failures++;report("FAIL: ");report(what);report("\n");}
}
#define CHECK(x) check(!!(x),#x)
static int test_write(const char *p,const u8 *b,u32 n)
{
    if(fail_after>=0&&write_count>=fail_after)return -2;
    int rc=real_api->fat_write(p,b,n);if(!rc)write_count++;return rc;
}
static int test_cancel(void){return cancel_after>=0&&write_count>=cancel_after;}
static int readonly_usb(void){return 0;}
static void fixture_add(const char *name,u32 n)
{
    api->memset(fixture+fixture_size,0,28);
    api->strlcpy((char *)fixture+fixture_size,name,24);
    u32 packed=lz_pack(payload,n,fixture+fixture_size+28,sizeof fixture-fixture_size-28);
    lz_put32(fixture+fixture_size+24,packed);fixture_size+=28+packed;fixture[4]++;
}
static int matches(const char *p,const u8 *bytes,u32 size)
{
    int n=api->fat_read(p,result,sizeof result);if(n!=(int)size)return 0;
    for(u32 i=0;i<size;i++)if(result[i]!=bytes[i])return 0;return 1;
}
static void run_tests(void *ctx)
{
    (void)ctx;api->timer_del(test_timer);
    CHECK(api->usb_present()&&api->fat_writable());
    if(!api->fat_writable())goto finish;
    CHECK(api->win_open(type)>=0);
    ar_empty(fixture);fixture_size=8;
    fixture_add("one.txt",sizeof payload-1);
    fixture_add("longfilename-one.txt",sizeof payload-1);
    fixture_add("longfilename-two.txt",sizeof payload-1);
    fixture_add("empty.bin",0);
    CHECK(api->fat_mkdir("/source")==0);
    CHECK(api->fat_write("/source/test.fpa",fixture,fixture_size)==0);
    CHECK(api->fat_write("/test.fpa",fixture,fixture_size)==0);
    CHECK(open_file("test.fpa","/source/test.fpa",0,0)==0);
    CHECK(count==4&&length==fixture_size&&!dirty);
    clear();CHECK(open_file("test.fpa","/test.fpa",0,0)==0);CHECK(count==4);
    clear();picked_open("U:/source/test.fpa",0);CHECK(count==4);
    u32 old_length=length;
    CHECK(api->fat_write("/bad.fpa",payload,sizeof payload-1)==0);
    picked_open("u:/bad.fpa",0);CHECK(count==4&&length==old_length);
    CHECK(api->fat_mkdir("/all")==0);extract_all=1;extract_to("u:/all/",0);
    CHECK(matches("/all/one.txt",payload,sizeof payload-1));
    CHECK(matches("/all/LONGFI~1.TXT",payload,sizeof payload-1));
    CHECK(matches("/all/LONGFI~2.TXT",payload,sizeof payload-1));
    CHECK(matches("/all/empty.bin",payload,0));
    CHECK(api->strcmp(message,"4 file(s) extracted. Long names shortened for USB.")==0);
    CHECK(api->fat_mkdir("/all/nested")==0);selected=0;extract_all=0;
    extract_to("U:/all/nested/",0);CHECK(matches("/all/nested/one.txt",payload,sizeof payload-1));
    extract_to("u:/",0);CHECK(matches("/one.txt",payload,sizeof payload-1));
    CHECK(api->fat_write("/one.txt",(const u8 *)"keep",4)==0);
    extract_to("u:/",0);CHECK(matches("/one.txt",(const u8 *)"keep",4));
    CHECK(api->strcmp(message,"A destination file already exists. Choose an empty folder.")==0);
    CHECK(api->fat_mkdir("/conflict")==0);CHECK(api->fat_mkdir("/conflict/one.txt")==0);
    extract_to("u:/conflict",0);CHECK(api->fat_exists("/conflict/one.txt")==2);
    CHECK(api->strcmp(message,"A destination file already exists. Choose an empty folder.")==0);
    CHECK(api->fat_mkdir("/alias")==0);CHECK(api->fat_write("/alias/LONGFI~1.TXT",(const u8 *)"keep",4)==0);
    selected=1;extract_to("u:/alias",0);
    CHECK(matches("/alias/LONGFI~1.TXT",(const u8 *)"keep",4));
    CHECK(matches("/alias/LONGFI~2.TXT",payload,sizeof payload-1));
    test_api.fat_writable=readonly_usb;extract_to("u:/source",0);
    CHECK(api->strcmp(message,"USB is missing or read-only.")==0);test_api.fat_writable=real_api->fat_writable;
    CHECK(api->fat_mkdir("/full")==0);write_count=0;fail_after=1;extract_all=1;
    extract_to("u:/full",0);CHECK(write_count==1&&matches("/full/one.txt",payload,sizeof payload-1));
    CHECK(api->strcmp(message,"1 file(s) extracted. Disk full; completed files were kept.")==0);fail_after=-1;
    CHECK(api->fat_mkdir("/cancel")==0);write_count=0;cancel_after=1;
    extract_to("u:/cancel",0);CHECK(write_count==1&&matches("/cancel/one.txt",payload,sizeof payload-1));
    CHECK(api->strcmp(message,"1 file(s) extracted. Stopped; completed files were kept.")==0);cancel_after=-1;
    extract_all=0;selected=0;extract_to("a:",0);
    CHECK(api->fs_read("one.txt",result,sizeof result)==sizeof payload-1);
    result[sizeof payload-1]=0;
    CHECK(api->strcmp((const char *)result,(const char *)payload)==0);
    CHECK(api->fs_write("local.fpa",fixture,fixture_size)==0);clear();
    CHECK(open_file("local.fpa",0,0,0)==0&&count==4);
    u32 packed=lz_pack(payload,sizeof payload-1,result,sizeof result);
    CHECK(api->fat_write("/old.txt.pz",result,packed)==0);clear();
    CHECK(open_file("old.txt.pz","/old.txt.pz",0,0)==0&&count==1);
    CHECK(!api->strcmp(entries[0].name,"old.txt"));
    extract_all=1;extract_to("u:/source",0);CHECK(matches("/source/old.txt",payload,sizeof payload-1));
    CHECK(matches("/source/test.fpa",fixture,fixture_size));
finish:
    {
        char text[100];api->kfmt(text,sizeof text,"ARCHIVE USB: %d pass %d fail\n",passes,failures);report(text);
    }
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Archive USB"};
int kext_entry(const Kapi *k)
{
    real_api=k;k->memcpy(&test_api,k,sizeof test_api);api=&test_api;
    test_api.fat_write=test_write;test_api.esc_pending=test_cancel;
    ui_init(api,0);
    static const AppDesc d={.live_draw=APP_INDEPENDENT,.title="Archive USB test",.max_inst=1,
        .open=opened,.close=closed,.draw=draw,.client_size=initial,.min_client=size};
    type=k->register_app(&d);if(type<0)return 1;
    k->outb(0x3f9,0);k->outb(0x3fb,0x80);k->outb(0x3f8,1);k->outb(0x3f9,0);k->outb(0x3fb,3);
    test_timer=k->timer_add(500,run_tests,0);return test_timer<0;
}
