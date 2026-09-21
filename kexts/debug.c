#include "kapi.h"
#include "debug.h"

static const Kapi *api;
static const DebugCore *core;
static DebugOps ops;
static int appid,focus,capture_busy,stop_pending;
static char capture_status[88],capture_name[20];
static u8 *capture_buf;
static u32 capture_bank;
static u32 fill[2],bank,packets,dropped,file_size,file_number,usb_gen,epoch,epoch_tick;
static u32 alloc_count,fs_count,disk_count,guard_bad;
static char alloc_line[2][88],fs_line[2][112],disk_line[4][112];
static const char *labels[]={"Capture network to USB (.pcap)","Log memory allocation failures",
    "Show all disk activity","Check stack guards","Show filesystem errors"};
#define CAP_BANK 524288u
#define CAP_FILE 1048576u

static u32 lock(void){u32 f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;}
static void unlock(u32 f){__asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");}
static void put32(u8 *p,u32 n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static u32 now_epoch(void)
{
    int h,m,s,d,mo,y;api->rtc_read(&h,&m,&s,&d,&mo,&y);
    static const int months[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<1970||y>2100||mo<1||mo>12||d<1||d>31)return 0;
    u32 days=0;
    for(int i=1970;i<y;i++)days+=365+(i%4==0&&(i%100!=0||i%400==0));
    for(int i=1;i<mo;i++)days+=months[i-1]+(i==2&&y%4==0&&(y%100!=0||y%400==0));
    return ((days+(u32)d-1)*24+(u32)h)*3600+(u32)m*60+(u32)s;
}
static int capture_file(const char *name)
{
    if(!name)return 0;
    for(const char *p=name;*p;p++)if(*p=='/')name=p+1;
    u32 n=api->strlen(name);
    if((n!=12&&n!=13)||(name[0]|32)!='n'||(name[1]|32)!='e'||(name[2]|32)!='t')return 0;
    for(u32 i=3;i<n-5;i++)if(name[i]<'0'||name[i]>'9')return 0;
    return !api->strcasecmp(name+n-5,".pcap");
}
static void event(u32 kind,const char *name,u32 a,u32 b,int result)
{
    u32 f=lock();char line[112];
    if(kind&DBG_ALLOC){
        api->kfmt(alloc_line[alloc_count++%2],88,"%s: %u bytes @%08x T%d",name?name:"alloc",a,b,result);
    }else{
        if(!result&&(b&0x80000000u)&&capture_file(name)){unlock(f);return;}
        int usb=(b>>31)!=0,write=(b&0x40000000u)!=0;
        if(!name&&!usb){
            if(!a)name="[boot]";
            else if(a<256)name="[kernel]";
            else if(a==256)name="[config]";
            else if(a<299)name="[FLOPFS metadata]";
            else for(int i=0;i<FS_NFILES;i++){
                const FsEnt *e=api->fs_slot(i);
                if(e&&e->used&&a>=e->start&&a-e->start<e->nsect){name=e->name;break;}
            }
        }
        if(!name)name=usb?"[USB metadata/raw]":"[raw]";
        if(a==~0u)api->kfmt(line,sizeof line,"%c:%c %s rc=%d",usb?'U':'A',write?'W':'R',name,result);
        else api->kfmt(line,sizeof line,"%c:%c LBA %u+%u %s%s",usb?'U':'A',write?'W':'R',a,b&0x3fffffffu,name,result?" ERROR":"");
        if((kind&DBG_FS)&&(ops.flags&DBG_FS)){
            fs_count++;api->strlcpy(fs_line[(fs_count-1)%2],line,sizeof fs_line[0]);
        }
        if((kind&DBG_DISK)&&(ops.flags&DBG_DISK)&&a!=~0u){
            disk_count++;api->strlcpy(disk_line[(disk_count-1)%4],line,sizeof disk_line[0]);
        }
    }
    unlock(f);api->gui_dirty();
}
static void packet(const u8 *frame,u32 size)
{
    u32 f=lock();
    if(!(ops.flags&DBG_NET)||!capture_buf){unlock(f);return;}
    if(size>1536||fill[bank]>capture_bank||size+16>capture_bank-fill[bank]){dropped++;unlock(f);return;}
    u8 *p=capture_buf+bank*capture_bank+fill[bank];u32 t=*api->ticks-epoch_tick;
    put32(p,epoch+t/100);put32(p+4,(t%100)*10000);put32(p+8,size);put32(p+12,size);
    api->memcpy(p+16,frame,size);fill[bank]+=size+16;packets++;unlock(f);
}
static int new_file(void)
{
    for(;file_number<10000;){
        api->kfmt(capture_name,sizeof capture_name,"/NET%04u.PCAP",++file_number);
        int exists=api->fat_exists(capture_name);if(exists<0)return 0;if(exists)continue;
        u8 h[24]={0};put32(h,0xa1b2c3d4);h[4]=2;h[6]=4;put32(h+16,1536);put32(h+20,1);
        if(api->fat_write(capture_name,h,sizeof h))return 0;
        file_size=24;return 1;
    }
    return 0;
}
static void capture_error(const char *s)
{
    u32 f=lock();ops.flags&=~DBG_NET;unlock(f);
    api->strlcpy(capture_status,s,sizeof capture_status);api->gui_dirty();
}
static int flush_capture(void)
{
    if(!capture_buf)return 1;
    if(!api->usb_present()||api->usb_gen()!=usb_gen){capture_error("Capture stopped: USB removed/changed");return 0;}
    u32 f=lock(),old=bank,n=fill[old];
    if(!n){unlock(f);return 1;}
    bank^=1;unlock(f);
    int ok=1;
    if(file_size+n>CAP_FILE&&!new_file())ok=0;
    if(ok&&core->append(capture_name,capture_buf+old*capture_bank,n))ok=0;
    f=lock();fill[old]=0;unlock(f);
    if(!ok){capture_error("Capture stopped: USB write failed/full");return 0;}
    file_size+=n;return 1;
}
static void capture_toggle(void)
{
    u32 gate=lock();
    if(capture_busy){if(ops.flags&DBG_NET)stop_pending=1;unlock(gate);return;}
    capture_busy=1;unlock(gate);
    if(ops.flags&DBG_NET){
        u32 f=lock();ops.flags&=~DBG_NET;unlock(f);
        if(flush_capture())api->strlcpy(capture_status,"Capture stopped",sizeof capture_status);
        api->kfree(capture_buf);capture_buf=0;
    }else{
        api->kfree(capture_buf);capture_buf=0;fill[0]=fill[1]=bank=packets=dropped=0;
        if(!api->usb_present()||!api->fat_mount()||!api->fat_writable())capture_error("Capture needs a writable USB");
        else{
            usb_gen=api->usb_gen();
            for(capture_bank=CAP_BANK;capture_bank>=4096;capture_bank/=2){
                capture_buf=api->kmalloc(capture_bank*2);if(capture_buf)break;
            }
            if(!capture_buf)capture_error("Capture stopped: allocation failed");
            else if(!new_file())capture_error("Capture stopped: cannot create PCAP");
            else{
                api->mem_track("Network capture",capture_buf,capture_bank*2);
                epoch=now_epoch();epoch_tick=*api->ticks;capture_status[0]=0;ops.flags|=DBG_NET;
            }
            if(!(ops.flags&DBG_NET)){api->kfree(capture_buf);capture_buf=0;}
        }
    }
    capture_busy=0;api->gui_dirty();
}
static void toggle(int row)
{
    if(row==0)capture_toggle();else{u32 f=lock();ops.flags^=1u<<row;unlock(f);}
    api->gui_dirty();
}
static void poll(void *ctx)
{
    (void)ctx;
    if(ops.flags&DBG_STACK){u32 bad=core->guards();if(bad!=guard_bad){guard_bad=bad;api->gui_dirty();}}
    if((ops.flags&DBG_NET)&&!capture_busy){
        u32 f=lock();if(capture_busy){unlock(f);return;}capture_busy=1;unlock(f);
        u32 pending=fill[bank];flush_capture();capture_busy=0;
        if(stop_pending){stop_pending=0;if(ops.flags&DBG_NET)capture_toggle();}
        if(pending)api->gui_dirty();
    }
}
static void shutdown(void)
{
    if((ops.flags&DBG_NET)&&!capture_busy)capture_toggle();
}
static void foreground(void)
{
    if(!(ops.flags&~DBG_NET))return;
    int width=*api->screen_w-8;if(width>504)width=504;
    int x=*api->screen_w-width-4,y=4;char s[88];
    int rows=0;if(ops.flags&DBG_ALLOC)rows+=3;if(ops.flags&DBG_DISK)rows+=5;
    if(ops.flags&DBG_STACK)rows++;if(ops.flags&DBG_FS)rows+=3;
    api->fill_rect(x,y,width,rows*16+8,C_BLACK);x+=4;y+=4;
#define LINE(t,c) do{api->draw_text_clip(x,y,t,c,width-8);y+=16;}while(0)
    if(ops.flags&DBG_ALLOC){
        api->kfmt(s,sizeof s,"Allocation failures: %u",alloc_count);LINE(s,C_WHITE);
        for(u32 i=alloc_count>2?alloc_count-2:0;i<alloc_count;i++)LINE(alloc_line[i%2],C_YELLOW);
        if(alloc_count<2)y+=(2-alloc_count)*16;
    }
    if(ops.flags&DBG_DISK){
        api->kfmt(s,sizeof s,"Disk activity: %u",disk_count);LINE(s,C_WHITE);
        for(u32 i=disk_count>4?disk_count-4:0;i<disk_count;i++)LINE(disk_line[i%4],C_WHITE);
        if(disk_count<4)y+=(4-disk_count)*16;
    }
    if(ops.flags&DBG_STACK){
        if(guard_bad)api->kfmt(s,sizeof s,"Stack guard FAILED: worker mask %02x",guard_bad);
        else api->strlcpy(s,"Stack guards: workers OK",sizeof s);
        LINE(s,guard_bad?C_RED:C_BGREEN);
    }
    if(ops.flags&DBG_FS){
        api->kfmt(s,sizeof s,"Filesystem errors: %u",fs_count);LINE(s,C_WHITE);
        for(u32 i=fs_count>2?fs_count-2:0;i<fs_count;i++)LINE(fs_line[i%2],C_YELLOW);
    }
#undef LINE
}
static void size(int inst,int *w,int *h){(void)inst;*w=352;*h=198;}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;(void)ch;
    for(int i=0;i<5;i++){
        int yy=y+8+i*26;api->rect(x+8,yy,14,14,i==focus?C_NAVY:C_BLACK);
        if(ops.flags&(1u<<i))api->draw_text(x+11,yy,"x",C_BLACK);
        api->draw_text_clip(x+30,yy,labels[i],C_BLACK,cw-38);
    }
    char s[88];api->kfmt(s,sizeof s,"U:%s",capture_name);
    if(capture_name[0]){
        api->draw_text_clip(x+8,y+140,s,C_BLACK,cw-16);
        api->kfmt(s,sizeof s,"Packets %u  lost %u",packets,dropped);
        api->draw_text_clip(x+8,y+158,s,C_BLACK,cw-16);
    }
    api->draw_text_clip(x+8,y+176,capture_status,C_MAROON,cw-16);
}
static void key(int inst,int k)
{
    (void)inst;if(k==K_UP)focus=(focus+4)%5;else if(k==K_DOWN||k=='\t')focus=(focus+1)%5;
    else if(k==' '||k=='\n')toggle(focus);
}
static void mouse(int inst,int x,int y,int ev,int cw,int ch)
{
    (void)inst;(void)cw;(void)ch;
    if(ev==EV_PRESS&&x>=8&&x<344&&y>=8&&y<138){focus=(y-8)/26;toggle(focus);}
}
static int hotkey(int k)
{
    if(k!=4)return 0;api->win_open(appid);core->place(appid);return 1;
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Debug"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;
    api=k;core=k->service_get("debug.core");if(!core||core->abi!=DEBUG_ABI)return 1;
    static const AppDesc d={.title="Debug",.max_inst=1,.in_menu=0,.draw=draw,
        .key=key,.mouse=mouse,.client_size=size,.live_draw=APP_INDEPENDENT};
    appid=api->register_app(&d);if(appid<0)return 1;
    ops.abi=DEBUG_ABI;ops.event=event;ops.draw=foreground;ops.packet=packet;
    if(api->register_service("debug",&ops)||api->register_key_hook(hotkey)||api->timer_add(5,poll,0)<0)return 1;
    api->register_shutdown(shutdown);core->bind(&ops);return 0;
}
