/* Writes crash details directly to the screen and serial port. */
#include "os.h"
#include "emergency_core.inc"
#include "panicnet.h"
extern const PanicMonitor *panic_monitor;
u32 panic_controls[3];
#include "panic_report.inc"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "vbank.inc"
#pragma clang diagnostic pop

u8 emergency_stack[8192] __attribute__((aligned(16)));
u32 emergency_pd[1024] __attribute__((aligned(4096)));
volatile u32 panic_active;
static EmergencyVideo em_video;
static u8 em_font[96*16];
static volatile u32 em_age;
static volatile int em_armed;
static u32 em_vec, em_err, em_eip, em_cr2;
static int em_x, em_y, em_bank, em_draw;

static inline u32 em_read_cr2(void)
{ u32 v; __asm__ volatile("mov %%cr2,%0" : "=r"(v)); return v; }

void emergency_init(void)
{
    for (u32 i=0;i<1024;i++) emergency_pd[i]=(i<<22)|0x83u;
}
void emergency_video(u32 base, int w, int h, int pitch, int bank)
{
    if (panic_active) return;
    em_video.base=base; em_video.width=w; em_video.height=h;
    em_video.pitch=pitch; em_video.bank=bank;
    em_video.check=em_video_check(&em_video);
    for (unsigned i=0;i<sizeof em_font;i++) em_font[i]=((volatile u8 *)FONT8x16)[32*16+i];
}
void emergency_watch_start(void)
{ em_age=0; em_armed=1; }
void emergency_heartbeat(void)
{
    if (em_armed && !panic_active && thr_self==0) em_age=0;
}
static void em_serial_init(void)
{
    outb(0x3f9,0); outb(0x3fb,0x80); outb(0x3f8,1); outb(0x3f9,0);
    outb(0x3fb,3); outb(0x3fa,7); outb(0x3fc,3);
}
static void em_serial(char c)
{
    for (unsigned n=0;n<2048;n++) if (inb(0x3fd)&0x20) { outb(0x3f8,(u8)c); break; }
}
static void em_pixel(u32 x, u32 y, u8 c)
{
    u32 off=y*em_video.pitch+x;
    if (em_video.bank) {
        int bank=off>>16;
        if (bank!=em_bank) {
            VbWrite writes[8]; int n=vbank_prog(em_video.bank,bank,writes);
            for (int i=0;i<n;i++) {
                VbWrite *w=&writes[i];
                if (w->op==VBOP_OUT) outb(w->port,w->val);
                else { outb(w->port,w->idx); if (w->op==VBOP_IDX) outb(w->port+1,w->val); else (void)inb(w->port+1); }
            }
            em_bank=bank;
        }
        off &= 65535u;
    }
    ((volatile u8 *)em_video.base)[off]=c;
}
static void em_line(const char *s, u8 color)
{
    int draw=em_draw && em_video_valid(&em_video);
    for (unsigned i=0;i<76 && s[i];i++) {
        u8 c=(u8)s[i]; em_serial((char)c);
        unsigned x=em_x+i*8;
        if (!draw || x+8>em_video.width || (u32)em_y+16>em_video.height) continue;
        if (c<32 || c>127) c='?';
        for (unsigned y=0;y<16;y++) {
            u8 row=em_font[(c-32)*16+y];
            for (unsigned bit=0;bit<8;bit++) if (row & (0x80u>>bit)) em_pixel(x+bit,em_y+y,color);
        }
    }
    em_serial('\r'); em_serial('\n'); em_y+=18;
}
static void em_field(const char *label, u32 v, int hex)
{
    char buf[64]; int n=pr_put(buf,sizeof buf,0,label);
    if (hex) pr_hex(buf,sizeof buf,n,v); else pr_dec(buf,sizeof buf,n,v);
    em_line(buf,C_WHITE);
}
static void em_palette(u8 i, u8 r, u8 g, u8 b)
{ outb(0x3c8,i); outb(0x3c9,r); outb(0x3c9,g); outb(0x3c9,b); }

__attribute__((noreturn)) void emergency_render(u32 vec,u32 err,u32 eip,u32 cr2,u32 reason)
{
    cli(); outb(0x21,0xff); outb(0xa1,0xff);
    if (panic_active>=2) for (;;) hlt();
    u32 original=panic_active;
    panic_active=2;
    em_serial_init();

    em_x=16; em_y=0;
    em_draw=0;
    em_line("FLOPNIX EMERGENCY KERNEL PANIC",C_WHITE);
    if (vec!=0xffffffffu) em_field("P",vec,0); else em_line("Watchdog deadline exceeded",C_WHITE);
    em_field("Instruction: ",eip,1); em_field("Error bits: ",err,1);
    em_field("Memory address: ",cr2,1);
    if (original==1) {
        em_field("Original exception: P",em_vec,0);
        em_field("Original error bits: ",em_err,1);
        em_field("Original memory address: ",em_cr2,1);
    }
    em_draw=em_video_valid(&em_video);
    em_bank=-1;
    if (em_video_valid(&em_video)) {
        em_palette(C_BLACK,0,0,0); em_palette(C_WHITE,63,63,63); em_palette(C_YELLOW,63,63,0);
        for (u32 y=0;y<em_video.height;y++) for (u32 x=0;x<em_video.width;x++) em_pixel(x,y,C_BLACK);
        em_x=em_video.width>640 ? (em_video.width-608)/2 : 16;
        em_y=em_video.height>220 ? (em_video.height-198)/2 : 4;
    }
    em_line("FLOPNIX stopped",C_WHITE);
    if (em_video_valid(&em_video)) for (u32 x=em_x;x<em_video.width-(u32)em_x;x++) em_pixel(x,em_y-1,C_WHITE);
    em_line("EMERGENCY KERNEL PANIC",C_YELLOW);
    em_line(reason==EM_DOUBLE ? "P8: Double fault" : reason==EM_STALL ? "Kernel stopped responding" : reason==EM_NMI ? "P2: Critical hardware interrupt" : "Exception reporting failed",C_WHITE);
    em_field("Instruction: ",eip,1);
    em_field("Error bits: ",err,1);
    if (original==1) { em_field("Original exception: P",em_vec,0); em_field("Original instruction: ",em_eip,1); if (em_vec==14) em_field("Original memory: ",em_cr2,1); }
    else { if (vec!=0xffffffffu) em_field("Exception: P",vec,0); else em_line("No processor exception was raised.",C_WHITE); em_field("Memory address: ",cr2,1); }
    em_line("System execution has been halted.",C_WHITE);
    em_line(panic_monitor ? "Starting LAN crash debugger..." : "Record these details, then restart.",C_WHITE);
    if(panic_monitor)panic_monitor->emergency(vec,err,eip,cr2,reason,original,em_vec,em_err,em_eip,em_cr2);
    for (;;) hlt();
}

void emergency_panic_begin(u32 vec,u32 err,u32 eip)
{
    cli();
    if (panic_active) emergency_enter(vec,err,eip,em_read_cr2(),EM_REPORT);
    em_vec=vec; em_err=err; em_eip=eip; em_cr2=vec==14 ? em_read_cr2() : 0;
    panic_active=1; em_age=0; em_armed=1;

    outb(0x21,0xfe); outb(0xa1,0xff);
    for (int i=0;i<8;i++) { outb(0xa0,0x20); outb(0x20,0x20); }
    if (timer_alive) sti();
}
int emergency_irq(u32 vec,u32 eip)
{
    if (vec==32) {

        if (!panic_active && kupd_critical) { em_age=0; return 0; }
        u32 age=em_age;
        int stop=em_watch_step(&age,em_armed,panic_active==1);
        em_age=age;
        if (stop) {
            outb(0x20,0x20);
            emergency_enter(0xffffffffu,0,eip,0,panic_active ? EM_REPORT : EM_STALL);
        }
    }
    if (panic_active && vec>=32 && vec<48) {
        if (vec>=40) outb(0xa0,0x20);
        outb(0x20,0x20); return 1;
    }
    return 0;
}
