#define kext_header debug_header
#define kext_entry debug_entry
#include "../kexts/debug.c"
#undef kext_header
#undef kext_entry

static const Kapi *real_api;
static Kapi test_api;
static int test_timer,passes,failures;
static u8 fixture[11003],result[11004],frame[60];
static void report(const char *s)
{
    while(*s){int guard=200000;while(!(api->inb(0x3fd)&0x20)&&--guard){}api->outb(0x3f8,(u8)*s++);}
}
static void check(int ok,const char *what)
{
    if(ok)passes++;else{failures++;report("FAIL: ");report(what);report("\n");}
}
#define CHECK(x) check(!!(x),#x)
static int compare(const u8 *a,const u8 *b,u32 n){while(n--)if(*a++!=*b++)return 1;return 0;}
static int no_usb(void){return 0;}
static int readonly_usb(void){return 0;}
static u32 changed_usb(void){return real_api->usb_gen()+1;}
static void *no_memory(u32 n){(void)n;return 0;}
static int fail_write(const char *p,const u8 *b,u32 n){(void)p;(void)b;(void)n;return -2;}
static u32 bad_guards(void){return 8;}
static u32 received,download_sum;
static u32 checksum(u32 sum,const u8 *p,u32 n){while(n--){sum=(sum<<5)|(sum>>27);sum+=*p++;}return sum;}
static int receive_kernel(const u8 *p,int n,void *ctx)
{
    (void)ctx;if(n<0||(u32)n>130560-received)return 0;
    download_sum=checksum(download_sum,p,(u32)n);received+=(u32)n;return 1;
}
static void run_tests(void *ctx)
{
    (void)ctx;api->timer_del(test_timer);
    DebugOps *production=(DebugOps *)api->service_get("debug");
    CHECK(production&&production->abi==DEBUG_ABI&&production->flags==0);
    int found=0;
    for(int i=0;i<api->app_count();i++){
        const AppDesc *d=api->app_desc(i);
        if(d&&!api->strcmp(d->title,"Debug")){found++;CHECK(!d->in_menu);}
    }
    CHECK(found==1);
    ops.abi=DEBUG_ABI;ops.event=event;ops.draw=foreground;ops.packet=packet;
    ops.flags=DBG_ALLOC|DBG_DISK|DBG_STACK|DBG_FS;core->bind(&ops);
    CHECK(core->guards()==0);
    u32 quiet=disk_count,err=fs_count;
    event(DBG_DISK,"/NET0001.PCAP",123,0xc0000001u,0);
    event(DBG_DISK,"/net10000.pcap",124,0x80000001u,0);
    CHECK(disk_count==quiet&&fs_count==err);
    event(DBG_DISK|DBG_FS,"/NET0001.PCAP",123,0xc0000001u,-1);
    CHECK(disk_count==quiet+1&&fs_count==err+1);
    event(DBG_FS,"/NET0001.PCAP",~0u,0xc0000000u,-2);
    CHECK(fs_count==err+2);
    event(DBG_DISK,"/other.pcap",125,0x80000001u,0);
    CHECK(disk_count==quiet+2);

    CHECK(api->kmalloc(0xffffffffu)==0);CHECK(alloc_count==1);
    u8 *p=api->kmalloc(32);CHECK(p!=0);
    if(p){p[0]=0x7a;CHECK(api->krealloc(p,0xffffffffu)==0);CHECK(p[0]==0x7a);api->kfree(p);}
    CHECK(alloc_count==2);
    ops.flags&=~DBG_ALLOC;api->kmalloc(0xffffffffu);CHECK(alloc_count==2);ops.flags|=DBG_ALLOC;
    CHECK(api->fs_read("missing-debug",result,1)<0);CHECK(fs_count>0);
    for(u32 i=0;i<sizeof fixture;i++)fixture[i]=(u8)(i*17+3);
    CHECK(api->fs_write("debug-read.bin",fixture,sizeof fixture)==0);
    u32 before=api->disk_stat(DS_OPS),start=*api->ticks;
    CHECK(api->fs_read("debug-read.bin",result,11002)==11002);
    u32 reads=api->disk_stat(DS_OPS)-before;
    CHECK(reads<=4&&reads>=2);CHECK(!compare(fixture,result,11002));
    char line[100];api->kfmt(line,sizeof line,"BATCH: 11002 bytes, %u disk commands, %u ticks\n",reads,*api->ticks-start);report(line);
    static const u32 lengths[]={0,1,511,512,513,1024,11003};
    for(u32 i=0;i<sizeof lengths/sizeof lengths[0];i++){
        u32 n=lengths[i];api->memset(result,0xcc,sizeof result);
        CHECK(api->fs_read("debug-read.bin",result,n)==(int)n);
        CHECK(!compare(fixture,result,n));CHECK(result[n]==0xcc);
    }
    CHECK(disk_count>0);CHECK(api->fs_delete("debug-read.bin")==0);
    const FsEnt *kernel=0;
    for(int i=0;i<FS_NFILES;i++){const FsEnt *e=api->fs_slot(i);if(e&&e->used&&!api->strcmp(e->name,"probe.ku"))kernel=e;}
    CHECK(kernel!=0);
    if(kernel){
        api->buffer_lock();u8 *image=kernel->size<=api->iobuf_size?api->iobuf:0;CHECK(image!=0);
        if(image){
            before=api->disk_stat(DS_OPS);start=*api->ticks;
            CHECK(api->fs_read("probe.ku",image,kernel->size)==(int)kernel->size);
            u32 batched=api->disk_stat(DS_OPS)-before,fast_ticks=*api->ticks-start;
            CHECK(batched<20);CHECK(image[0]==0xeb);
            before=api->disk_stat(DS_OPS);start=*api->ticks;int same=1;
            for(u32 i=0;i<kernel->size/512;i++)if(api->disk_read(kernel->start+i,result)||compare(result,image+i*512,512)){same=0;break;}
            CHECK(same);CHECK(api->disk_stat(DS_OPS)-before==kernel->size/512);
            api->kfmt(line,sizeof line,"KERNEL READ: batch %u commands/%u ticks; sector %u commands/%u ticks\n",batched,fast_ticks,api->disk_stat(DS_OPS)-before,*api->ticks-start);report(line);
        }
        api->buffer_unlock();
    }

    CHECK(api->fat_mount()&&api->fat_writable());
    CHECK(api->fat_write("/DBGUSB.BIN",fixture,sizeof fixture)==0);
    CHECK(api->fat_read("/DBGUSB.BIN",result,sizeof result)==sizeof fixture);
    CHECK(!compare(fixture,result,sizeof fixture));
    u32 errors=fs_count;CHECK(api->fat_read("/missing-debug",result,1)<0);CHECK(fs_count>errors);
    test_api.usb_present=no_usb;capture_toggle();CHECK(!(ops.flags&DBG_NET));test_api.usb_present=real_api->usb_present;
    test_api.fat_writable=readonly_usb;capture_toggle();CHECK(!(ops.flags&DBG_NET));test_api.fat_writable=real_api->fat_writable;
    test_api.kmalloc=no_memory;capture_toggle();CHECK(!(ops.flags&DBG_NET)&&!capture_buf);test_api.kmalloc=real_api->kmalloc;
    report("CAPTURE: enable\n");capture_toggle();CHECK((ops.flags&DBG_NET)&&capture_buf);CHECK(file_size==24);
    DebugOps saved=*production;*production=ops;
    CHECK(api->net_up());CHECK(api->net_get(NET_IP)!=0);
    int ping=api->net_ping(api->net_get(NET_GW),400);CHECK(ping>=0);CHECK(packets>=2);report("CAPTURE: ping complete\n");
    *production=saved;
    api->memset(frame,0,sizeof frame);frame[0]=2;frame[6]=2;frame[12]=0x88;frame[13]=0xb5;
    if(api->mem_info(MI_TOTAL_KB)>=16384){
        CHECK(capture_bank>=524288);
        u32 loss=dropped,first=fill[bank];
        for(int i=0;i<800;i++)packet(fixture,590);
        CHECK(dropped==loss);CHECK(fill[bank]==first+800*606);
        CHECK(!compare(capture_buf+bank*capture_bank+first+799*606+16,fixture,590));
        fill[bank]=first;
    }
    CHECK(flush_capture());
    u32 saved_bank=bank;bank=0;
    u32 saved_fill=fill[bank],loss=dropped,other=bank^1;
    api->memset(capture_buf+other*capture_bank,0xa7,16);
    fill[bank]=capture_bank-76;packet(frame,sizeof frame);CHECK(fill[bank]==capture_bank);
    packet(frame,sizeof frame);CHECK(dropped==loss+1);
    int intact=1;for(int i=0;i<16;i++)if(capture_buf[other*capture_bank+i]!=0xa7)intact=0;
    CHECK(intact);
    fill[bank]=capture_bank+1;packet(frame,sizeof frame);CHECK(dropped==loss+2);
    fill[bank]=saved_fill;packet(frame,0xffffffffu);CHECK(dropped==loss+3&&fill[bank]==saved_fill);
    bank=saved_bank;
    for(u32 i=0;i<capture_bank/76+2;i++)packet(frame,sizeof frame);
    CHECK(dropped>loss+3);CHECK(fill[bank]<=capture_bank);
    if(fill[bank]>76*32)fill[bank]=76*32;
    CHECK(flush_capture());CHECK(file_size>24);
    file_size=CAP_FILE;packet(frame,sizeof frame);CHECK(flush_capture());CHECK(file_number==2);
    capture_toggle();CHECK(!(ops.flags&DBG_NET)&&!capture_buf);
    capture_toggle();CHECK((ops.flags&DBG_NET)&&file_number==3);
    test_api.usb_gen=changed_usb;packet(frame,sizeof frame);CHECK(!flush_capture());
    CHECK(!(ops.flags&DBG_NET));test_api.usb_gen=real_api->usb_gen;
    capture_toggle();CHECK((ops.flags&DBG_NET)&&file_number==4);
    DebugCore mock=*core;const DebugCore *saved_core=core;mock.append=fail_write;core=&mock;
    packet(frame,sizeof frame);CHECK(!flush_capture());CHECK(!(ops.flags&DBG_NET));core=saved_core;
    api->kfree(capture_buf);capture_buf=0;
    if(api->mem_info(MI_TOTAL_KB)>=16384&&kernel){
        report("CAPTURE: HTTP download\n");
        CHECK(api->fs_read("http-port",result,2)==2);u16 port=result[0]|((u16)result[1]<<8);
        api->buffer_lock();CHECK(api->fs_read("probe.ku",api->iobuf,kernel->size)==(int)kernel->size);
        u32 expected=checksum(0,api->iobuf,kernel->size);api->buffer_unlock();
        capture_toggle();CHECK((ops.flags&DBG_NET)&&file_number==5);
        *production=ops;u32 ip=0;CHECK(api->net_parse_ip("10.0.2.2",&ip));
        int status=api->net_http_get(ip,port,"10.0.2.2","/kernel.ku",receive_kernel,0,1000);
        *production=saved;
        CHECK(status==200&&received==kernel->size&&download_sum==expected);CHECK(dropped==0);
        api->kfmt(line,sizeof line,"KERNEL CAPTURE: %u bytes, %u packets, %u lost\n",received,packets,dropped);report(line);
        capture_toggle();CHECK(!(ops.flags&DBG_NET)&&!capture_buf);
    }
    mock=*core;mock.guards=bad_guards;core=&mock;poll(0);CHECK(guard_bad==8);core=saved_core;
    poll(0);CHECK(guard_bad==0);
    ops.flags=0;core->bind(production);
    api->kfmt(line,sizeof line,"DEBUG TEST: %d pass %d fail\n",passes,failures);report(line);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Debug test"};
int kext_entry(const Kapi *k)
{
    real_api=k;k->memcpy(&test_api,k,sizeof test_api);api=&test_api;
    core=k->service_get("debug.core");if(!core||core->abi!=DEBUG_ABI)return 1;
    k->outb(0x3f9,0);k->outb(0x3fb,0x80);k->outb(0x3f8,1);k->outb(0x3f9,0);k->outb(0x3fb,3);
    test_timer=k->timer_add(300,run_tests,0);return test_timer<0;
}
