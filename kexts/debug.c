#include "kapi.h"
#include "debug.h"
#include "debugnet.h"

static const Kapi *api;
static const DebugCore *core;
static DebugOps ops;
static int appid,focus,toprow,capture_busy,stop_pending;
static char capture_status[88],capture_name[20];
static u8 *capture_buf;
static u32 capture_bank;
static u32 fill[2],bank,packets,dropped,file_size,file_number,usb_gen,epoch,epoch_tick;
static u32 alloc_count,fs_count,disk_count,guard_bad;
static char alloc_line[2][88],fs_line[2][112],disk_line[4][112];
static const char *labels[]={"Capture network to USB (.pcap)","Log memory allocation failures",
    "Show all disk activity","Check stack guards","Show filesystem errors","Poison freed memory","Automatic Remote",
    "Show device changes","Highlight redraw regions","Check heap integrity","Show input backlog","Show slow handlers","Find crash receiver"};
static const u32 row_flags[]={DBG_NET,DBG_ALLOC,DBG_DISK,DBG_STACK,DBG_FS,DBG_POISON,0,
    DBG_DEVICE,DBG_REDRAW,DBG_HEAP,DBG_BACKLOG,DBG_SLOW,0};
#define SAVED_FLAGS (DBG_DEVICE|DBG_BACKLOG)
#define VISIBLE_ROWS 7
static int view_rows=VISIBLE_ROWS;
#define ROWS ((int)(sizeof labels/sizeof labels[0]))
#define ROW_H 22
#define CAP_BANK 524288u
#define CAP_FILE 1048576u

static u32 lock(void){u32 f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;}
static void unlock(u32 f){__asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");}
#include "debugheap.inc"
#include "debugredraw.inc"
#include "debugdiag.inc"

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
    if(kind==DBG_SLOW&&a<10)return;
    u32 f=lock();char line[112];
    if(kind==DBG_SLOW){
        static const char *names[]={"key","mouse","wheel","callback","drop"};
        api->kfmt(slow_line[slow_count++%2],sizeof slow_line[0],"%s: %s %ums",
            name?name:"App",b<5?names[b]:"handler",a*10);
    }else if(kind&DBG_ALLOC){
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
static int saved_on(const char *key)
{
    u32 value=0;return api->config_get(key,&value)&&value==1;
}
static void toggle(int row)
{
    if(row==6||row==ROWS-1){
        const char *key=row==6?"remote.auto":"debugnet.auto";
        int value=!saved_on(key);
        if(!api->config_set(key,(u32)value))api->notify("Cannot save debug setting. Check the boot floppy.");
        else if(row==6)api->notify(value?"Automatic Remote saved: on at next boot.":"Automatic Remote saved: off at next boot.");
        else{
            const NetDebugOps *net=api->service_get("net.debug");
            if(net&&net->abi==NET_DEBUG_ABI)net->set_auto(value);
            api->notify(value?"Find crash receiver saved: on.":"Find crash receiver saved: off.");
        }
    }else if(row==0)capture_toggle();
    else{
        u32 bit=row_flags[row],next=ops.flags^bit;
        if((bit&SAVED_FLAGS)&&!api->config_set("debug.flags",next&SAVED_FLAGS)){
            api->notify("Cannot save debug setting. Check the boot floppy.");return;
        }
        u32 f=lock();ops.flags^=bit;
        if(bit==DBG_DEVICE)device_valid=0;
        if(bit==DBG_HEAP){heap_status=-3;heap_bad=0;diag_tick=*api->ticks-100;}
        if(bit==DBG_SLOW){slow_count=0;slow_active[0]=slow_line[0][0]=slow_line[1][0]=0;}
        unlock(f);
    }
    api->gui_dirty();
}
static void poll(void *ctx)
{
    (void)ctx;
    diagnostics_poll();
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
static int debug_overlap(int x,int y,int width,int height)
{
    for(int i=0;i<api->win_max();i++){
        const Win *w=api->win_slot(i);
        if(w&&w->type==appid&&x<w->x+w->w&&x+width>w->x&&y<w->y+w->h&&y+height>w->y)return 1;
    }
    return 0;
}
static void foreground(int win)
{
    if(win==-2){redraw_restore();return;}
    if(!ops.flags)return;
    int marker_x=*api->screen_w-52,marker_y=*api->screen_h-52;
    if(!debug_overlap(marker_x,marker_y,48,20)){
        api->fill_rect(marker_x,marker_y,48,20,C_BLACK);
        api->draw_text(marker_x+4,marker_y+2,"DEBUG",C_YELLOW);
    }
    if(!(ops.flags&(DBG_ALLOC|DBG_DISK|DBG_STACK|DBG_FS|DBG_DEVICE|DBG_HEAP|DBG_BACKLOG|DBG_SLOW))){
        if(ops.flags&DBG_REDRAW)redraw_outline(win);return;
    }
    int width=*api->screen_w-8;if(width>504)width=504;
    int x=*api->screen_w-width-4,y=4;char s[88];
    int rows=0;
    for(u32 bit=DBG_ALLOC;bit<=DBG_BACKLOG;bit<<=1)
        if((bit&(DBG_ALLOC|DBG_DISK|DBG_STACK|DBG_FS|DBG_DEVICE|DBG_HEAP|DBG_BACKLOG))&&(ops.flags&bit))rows++;
    if(ops.flags&DBG_SLOW)rows+=2;
    int spare=(*api->screen_h-48)/16-rows;if(spare<0)spare=0;
    int details[5]={0},limits[5]={2,4,2,4,2};
    u32 kinds[5]={DBG_ALLOC,DBG_DISK,DBG_FS,DBG_DEVICE,DBG_SLOW};
    for(int i=0;i<5;i++)if(ops.flags&kinds[i]){
        details[i]=spare<limits[i]?spare:limits[i];spare-=details[i];rows+=details[i];
    }
    if(debug_overlap(x,y,width,rows*16+8)){
        if(ops.flags&DBG_REDRAW)redraw_outline(win);return;
    }
    api->set_clip(0,0,*api->screen_w,*api->screen_h-32);
    api->fill_rect(x,y,width,rows*16+8,C_BLACK);x+=4;y+=4;
#define LINE(t,c) do{api->draw_text_clip(x,y,t,c,width-8);y+=16;}while(0)
    if(ops.flags&DBG_ALLOC){
        api->kfmt(s,sizeof s,"Allocation failures: %u",alloc_count);LINE(s,C_WHITE);
        for(u32 i=alloc_count>(u32)details[0]?alloc_count-details[0]:0;i<alloc_count;i++)LINE(alloc_line[i%2],C_YELLOW);
        if(alloc_count<(u32)details[0])y+=(details[0]-alloc_count)*16;
    }
    if(ops.flags&DBG_DISK){
        api->kfmt(s,sizeof s,"Disk activity: %u",disk_count);LINE(s,C_WHITE);
        for(u32 i=disk_count>(u32)details[1]?disk_count-details[1]:0;i<disk_count;i++)LINE(disk_line[i%4],C_WHITE);
        if(disk_count<(u32)details[1])y+=(details[1]-disk_count)*16;
    }
    if(ops.flags&DBG_STACK){
        if(guard_bad)api->kfmt(s,sizeof s,"Stack guard FAILED: worker mask %02x",guard_bad);
        else api->strlcpy(s,"Stack guards: workers OK",sizeof s);
        LINE(s,guard_bad?C_RED:C_BGREEN);
    }
    if(ops.flags&DBG_FS){
        api->kfmt(s,sizeof s,"Filesystem errors: %u",fs_count);LINE(s,C_WHITE);
        for(u32 i=fs_count>(u32)details[2]?fs_count-details[2]:0;i<fs_count;i++)LINE(fs_line[i%2],C_YELLOW);
        if(fs_count<(u32)details[2])y+=(details[2]-fs_count)*16;
    }
    if(ops.flags&DBG_DEVICE){
        api->kfmt(s,sizeof s,"Device changes: %u (1s samples)",device_count);LINE(s,C_WHITE);
        for(u32 i=device_count>(u32)details[3]?device_count-details[3]:0;i<device_count;i++)LINE(device_line[i%4],C_YELLOW);
        if(device_count<(u32)details[3])y+=(details[3]-device_count)*16;
    }
    if(ops.flags&DBG_HEAP){
        heap_message(s,sizeof s);LINE(s,heap_status>0?C_RED:heap_status?C_YELLOW:C_BGREEN);
    }
    if(ops.flags&DBG_BACKLOG){
        api->kfmt(s,sizeof s,"Input backlog: %u  peak %u  dropped %u",queue_depth,queue_peak,queue_dropped);LINE(s,C_WHITE);
    }
    if(ops.flags&DBG_SLOW){
        api->kfmt(s,sizeof s,"Slow handlers (100ms+): %u done",slow_count);LINE(s,C_WHITE);
        LINE(slow_active[0]?slow_active:"No handler over 100ms",C_YELLOW);
        for(u32 i=slow_count>(u32)details[4]?slow_count-details[4]:0;i<slow_count;i++)LINE(slow_line[i%2],C_YELLOW);
    }
    api->clear_clip();
    if(ops.flags&DBG_REDRAW)redraw_outline(win);
#undef LINE
}
static void size(int inst,int *w,int *h){(void)inst;*w=352;*h=198+ROW_H;}
static void keep_focus(void);
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;view_rows=(ch-64)/ROW_H;
    if(view_rows<1)view_rows=1;if(view_rows>VISIBLE_ROWS)view_rows=VISIBLE_ROWS;
    keep_focus();
    for(int i=toprow;i<ROWS&&i<toprow+view_rows;i++){
        int yy=y+8+(i-toprow)*ROW_H;api->rect(x+8,yy,14,14,i==focus?C_NAVY:C_BLACK);
        if(i==6?saved_on("remote.auto"):i==ROWS-1?saved_on("debugnet.auto"):(ops.flags&row_flags[i]))api->draw_text(x+11,yy,"x",C_BLACK);
        api->draw_text_clip(x+30,yy,labels[i],C_BLACK,cw-54);
    }
    api->draw_char(x+cw-16,y+8,0x1e,toprow?C_BLACK:C_GRAY);
    api->draw_char(x+cw-16,y+8+(view_rows-1)*ROW_H,0x1f,toprow+view_rows<ROWS?C_BLACK:C_GRAY);
    char s[88];api->kfmt(s,sizeof s,"U:%s",capture_name);
    if(capture_name[0]){
        api->draw_text_clip(x+8,y+8+view_rows*ROW_H,s,C_BLACK,cw-16);
        api->kfmt(s,sizeof s,"Packets %u  lost %u",packets,dropped);
        api->draw_text_clip(x+8,y+26+view_rows*ROW_H,s,C_BLACK,cw-16);
    }
    api->draw_text_clip(x+8,y+44+view_rows*ROW_H,capture_status,C_MAROON,cw-16);
}
static void keep_focus(void)
{
    if(focus<toprow)toprow=focus;
    if(focus>=toprow+view_rows)toprow=focus-view_rows+1;
}
static void key(int inst,int k)
{
    (void)inst;if(k==K_UP)focus=(focus+ROWS-1)%ROWS;else if(k==K_DOWN||k=='\t')focus=(focus+1)%ROWS;
    else if(k==' '||k=='\n')toggle(focus);
    keep_focus();api->gui_dirty();
}
static void wheel(int inst,int dz)
{
    (void)inst;toprow-=dz;
    if(toprow<0)toprow=0;if(toprow>ROWS-view_rows)toprow=ROWS-view_rows;
    if(focus<toprow)focus=toprow;if(focus>=toprow+view_rows)focus=toprow+view_rows-1;
    api->gui_dirty();
}
static void mouse(int inst,int x,int y,int ev,int cw,int ch)
{
    (void)inst;(void)ch;
    if(ev!=EV_PRESS||y<8||y>=8+view_rows*ROW_H)return;
    if(x>=cw-24&&x<cw){wheel(0,y<8+view_rows*ROW_H/2?1:-1);return;}
    if(x>=8&&x<cw-24){focus=toprow+(y-8)/ROW_H;toggle(focus);}
}
static int hotkey(int k)
{
    if(k!=4)return 0;api->win_open(appid);core->place(appid);return 1;
}
static void snapshot(char *out,u32 cap)
{
    api->kfmt(out,cap,"Debug snapshot at tick %u (last healthy poll)\nFlags %x\nAllocation failures %u\n%s\n%s\nFilesystem errors %u\n%s\n%s\nDisk events %u\n%s\n%s\n%s\n%s\nStack guard mask %x\nCapture %s: %s\nPackets %u; recorder drops %u; file bytes %u\nBuffer fill %u/%u; bank %u; bank capacity %u\n",
        *api->ticks,ops.flags,alloc_count,alloc_line[0],alloc_line[1],fs_count,fs_line[0],fs_line[1],
        disk_count,disk_line[0],disk_line[1],disk_line[2],disk_line[3],guard_bad,
        capture_name,capture_status,packets,dropped,file_size,fill[0],fill[1],bank,capture_bank);
    u32 n=api->strlen(out);if(n<cap)diagnostics_snapshot(out+n,cap-n);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Debug"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;
    api=k;core=k->service_get("debug.core");if(!core||core->abi!=DEBUG_ABI)return 1;
    static const AppDesc d={.title="Debug",.max_inst=1,.in_menu=0,.draw=draw,
        .key=key,.mouse=mouse,.wheel=wheel,.client_size=size,.live_draw=APP_INDEPENDENT};
    appid=api->register_app(&d);if(appid<0)return 1;
    u32 saved=0;api->config_get("debug.flags",&saved);ops.flags=saved&SAVED_FLAGS;
    diag_tick=*api->ticks-100;
    ops.abi=DEBUG_ABI;ops.event=event;ops.draw=foreground;ops.packet=packet;ops.snapshot=snapshot;
    if(api->register_service("debug",&ops)||api->register_key_hook(hotkey)||api->timer_add(5,poll,0)<0)return 1;
    api->register_shutdown(shutdown);core->bind(&ops);return 0;
}
