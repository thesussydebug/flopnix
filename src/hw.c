/* Handles interrupts, the clock, and keyboard and mouse input. */
#include "os.h"
#include "ring3.inc"
#include "ms2core.inc"

volatile u32 ticks;

typedef struct __attribute__((packed)) {
    u16 lo, sel;
    u8  zero, flags;
    u16 hi;
} IdtEntry;

typedef struct __attribute__((packed)) {
    u16 limit;
    u32 base;
} Idtr;

static IdtEntry idt[256];
static Idtr idtr;

extern u32 isr_table[48];
extern void idt_load(void *);

static void idt_set(int v, u32 handler)
{
    idt[v].lo = handler & 0xFFFF;
    idt[v].sel = 0x08;
    idt[v].zero = 0;
    idt[v].flags = 0x8E;
    idt[v].hi = handler >> 16;
}

void idt_set_user_gate(int vec, u32 handler)
{
    idt_set(vec, handler);
    idt[vec].flags = (u8)r3_gate_flags(3);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u32)idt;
    idt_load(&idtr);
}

void idt_set_task_gate(int vec,u16 selector)
{
    idt[vec].lo=idt[vec].hi=0; idt[vec].zero=0;
    idt[vec].sel=selector; idt[vec].flags=0x85;
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        idt_set(i, isr_table[i]);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u32)idt;
    idt_load(&idtr);
}

void pic_init(void)
{
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xB8); io_wait();
    outb(0xA1, 0xEF); io_wait();

    outb(0x4D0, inb(0x4D0) & (u8)~0x07); io_wait();
    outb(0x4D1, inb(0x4D1) & (u8)~0x21); io_wait();
}

void pit_init(void)
{
    u32 div = 1193182 / 100;
    outb(0x43, 0x36); io_wait();
    outb(0x40, div & 0xFF); io_wait();
    outb(0x40, div >> 8); io_wait();
}

static volatile u8  kq[64];
static volatile u32 kq_h, kq_t;
static volatile u32 mq[32];
static volatile u32 mq_time[32];
static volatile u32 mq_h, mq_t;

int kbd_pop(u8 *sc)
{
    if (kq_h == kq_t) return 0;
    *sc = kq[kq_h & 63];
    kq_h++;
    return 1;
}

int kbd_cancel_pending(void)
{
    u32 f = irq_save();
    int esc = 0;
    for (u32 i = kq_h; i != kq_t; i++)
        if (kq[i & 63] == 0x01) { esc = 1; break; }
    irq_restore(f);
    return esc;
}

int mouse_pop(u32 *pk, u32 *when)
{
    if (mq_h == mq_t) return 0;
    *pk = mq[mq_h & 31];

    if (when) *when = mq_time[mq_h & 31];
    mq_h++;
    return 1;
}

static u8 mouse_wheel;

int mouse_has_wheel(void) { return mouse_wheel; }

static Ms2Asm masm;
static void drain_8042(void)
{
    for (;;) {
        u8 st = inb(0x64);
        if (!(st & 0x01)) return;
        u8 b = inb(0x60);
        if (st & 0x20) {
            u32 pk;
            if (ms2_feed(&masm, b, ticks, mouse_wheel ? 4 : 3, &pk) &&
                (u32)(mq_t - mq_h) < 32) {
                mq[mq_t & 31] = pk;
                mq_time[mq_t & 31] = ticks;
                mq_t++;
            }
        } else if ((u32)(kq_t - kq_h) < 64) {
            kq[kq_t & 63] = b;
            kq_t++;
        }
    }
}

static void kbd_isr(void)   { drain_8042(); }
static void mouse_isr(void) { drain_8042(); }

volatile u32 irq_counts[16];

void pic_mask(int irq)
{
    if (irq < 8) outb(0x21, inb(0x21) | (1 << irq));
    else         outb(0xA1, inb(0xA1) | (1 << (irq - 8)));
}

static void pic_unmask(int irq)
{
    if (irq < 8) outb(0x21, inb(0x21) & ~(1 << irq));
    else         outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
}

static void (*irq_fns[16])(void);

static int irq_owner[16] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };

int irq_kext_busy(int owner)
{
    for (int i=0;i<16;i++) if (irq_fns[i] && irq_owner[i]==owner) return 1;
    return 0;
}

int irq_register(int irq, void (*fn)(void))
{
    if (irq < 0 || irq > 15 || !fn || irq_fns[irq]) return -1;
    irq_fns[irq] = fn;
    irq_owner[irq] = kext_owner_now();
    pic_unmask(irq);
    if (irq >= 8) pic_unmask(2);
    return 0;
}

void irq_unregister(int irq)
{
    if (irq < 0 || irq > 15) return;
    irq_fns[irq] = 0;
    switch (irq) {
    case 0: case 1: case 2: case 6: case 12: break;
    default: pic_mask(irq);
    }
}

volatile int in_irq;

void isr_dispatch(u32 vec, u32 err, u32 eip)
{
    if (panic_active>=2) { cli(); for (;;) hlt(); }
    if (vec==2) emergency_enter(vec,err,eip,0,EM_NMI);
    if (emergency_irq(vec,eip)) return;
    if (vec < 32) { fault_handle(vec, err, eip); return; }

    if (vec >= 48) return;

    in_irq++;
    u32 irq = vec - 32;
    if (irq < 16) irq_counts[irq]++;
    if (irq == 7) {
        outb(0x20, 0x0B);
        if (!(inb(0x20) & 0x80)) { in_irq--; return; }
    }
    if (irq == 15) {
        outb(0xA0, 0x0B);
        if (!(inb(0xA0) & 0x80)) { outb(0x20, 0x20); in_irq--; return; }
    }

    switch (irq) {
    case 0:  ticks++; break;
    case 1:  kbd_isr(); break;
    case 6:  fdc_irq_fl = 1; break;
    case 12: mouse_isr(); break;
    default: break;
    }
    if (irq < 16 && irq_fns[irq]) {

        int resident = kext_current();
        kext_enter(irq_owner[irq]);
        irq_fns[irq]();
        kext_enter(resident);
    }

    int outermost = (in_irq == 1);
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    in_irq--;

    /* Switch threads after the interrupt has been acknowledged. */
    if (irq == 0 && outermost) thr_tick();
}

static int kbc_wait_in(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2)) return 1;
    return 0;
}

static int kbc_wait_out(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return 1;
    return 0;
}

static void kbc_flush(void)
{
    for (int i = 0; i < 32 && (inb(0x64) & 1); i++)
        inb(0x60);
}

void kbd_init(void)
{
    kbc_flush();
}

int kbd_present(void)
{
    return inb(0x64) != 0xFF;
}

static int mouse_write(u8 v)
{
    if (!kbc_wait_in()) return 0;
    outb(0x64, 0xD4);
    if (!kbc_wait_in()) return 0;
    outb(0x60, v);
    if (kbc_wait_out()) return inb(0x60) == 0xFA;
    return 0;
}

static u8 mouse_id(void)
{
    mouse_write(0xF2);
    if (!kbc_wait_out()) return 0;
    return inb(0x60);
}

int mouse_init(void)
{
    kbc_flush();
    if (!kbc_wait_in()) return 0;
    outb(0x64, 0xA8);

    int ok = mouse_write(0xF6);

    mouse_write(0xF3); mouse_write(0xC8);
    mouse_write(0xF3); mouse_write(0x64);
    mouse_write(0xF3); mouse_write(0x50);
    mouse_wheel = (mouse_id() == 0x03);

    ok &= mouse_write(0xF4);
    kbc_flush();

    if (!kbc_wait_in()) return ok;
    outb(0x64, 0x20);
    if (!kbc_wait_out()) return ok;
    u8 cb = inb(0x60);
    cb |= 0x03;
    cb &= (u8)~0x30;
    if (!kbc_wait_in()) return ok;
    outb(0x64, 0x60);
    if (!kbc_wait_in()) return ok;
    outb(0x60, cb);
    kbc_flush();
    return ok;
}

void speaker_tone(u32 hz)
{
    if (hz < 20 || hz > 20000) return;
    u32 div = 1193182 / hz;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    outb(0x61, inb(0x61) | 3);
}

void speaker_off(void)
{
    outb(0x61, inb(0x61) & (u8)~3);
}

int plat_emulated;
u32 bda_ebda_seg, bda_base_mem_kb;

u32 pci_cfg_read(u8 bus, u8 dev, u8 fn, u8 off)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                ((u32)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

void pci_cfg_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                ((u32)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

int pci_find(u16 vendor, u16 device, int *bus, int *dev, int *fn)
{
    for (int b = 0; b < 4; b++)
        for (int d = 0; d < 32; d++)
            for (int f = 0; f < 8; f++) {
                u32 id = pci_cfg_read(b, d, f, 0);
                if (id == 0xFFFFFFFF) { if (!f) break; continue; }
                if ((id & 0xFFFF) == vendor && (id >> 16) == device) {
                    if (bus) *bus = b;
                    if (dev) *dev = d;
                    if (fn)  *fn = f;
                    return 1;
                }
                if (!f && !(pci_cfg_read(b, d, 0, 0x0C) & 0x800000))
                    break;
            }
    return 0;
}

static u8 cmos(u8 r)
{
    outb(0x70, r);
    return inb(0x71);
}

static void cmos_w(u8 r, u8 v)
{
    outb(0x70, r);
    outb(0x71, v);
}

static int frombcd(u8 v) { return (v >> 4) * 10 + (v & 0x0F); }
static u8  tobcd(int v)  { return (u8)(((v / 10) << 4) | (v % 10)); }

void rtc_read(int *h, int *m, int *s, int *D, int *M, int *Y)
{
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) ;
    u8 ss = cmos(0), mm = cmos(2), hh = cmos(4);
    u8 d = cmos(7), mo = cmos(8), yr = cmos(9);
    u8 st = cmos(0x0B);
    int pm = hh & 0x80;
    hh &= 0x7F;
    if (!(st & 4)) {
        ss = frombcd(ss); mm = frombcd(mm); hh = frombcd(hh);
        d = frombcd(d); mo = frombcd(mo); yr = frombcd(yr);
    }
    if (!(st & 2)) {
        if (hh == 12) hh = 0;
        if (pm) hh += 12;
    }
    *h = hh; *m = mm; *s = ss;
    *D = d; *M = mo; *Y = 2000 + yr;
}

u32 rtc_now_dos(void)
{
    int h, m, s, D, M, Y;
    rtc_read(&h, &m, &s, &D, &M, &Y);
    u32 date = (((u32)(Y - 1980) & 0x7F) << 9) | ((u32)M << 5) | (u32)D;
    u32 time = ((u32)h << 11) | ((u32)m << 5) | ((u32)s >> 1);
    return (date << 16) | time;
}

void dos_fmt(u32 dt, char *buf)
{
    if (!dt) { strlcpy(buf, "-", 16); return; }
    int Y = 1980 + ((dt >> 25) & 0x7F);
    int M = (dt >> 21) & 0x0F;
    int D = (dt >> 16) & 0x1F;
    int h = (dt >> 11) & 0x1F;
    int m = (dt >> 5) & 0x3F;
    kfmt(buf, 16, "%02d-%02d-%02d %02d:%02d", Y % 100, M, D, h, m);
}

int rtc_write(int h, int m, int s, int D, int M, int Y)
{
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 ||
        D < 1 || D > 31 || M < 1 || M > 12 || Y < 2000 || Y > 2099)
        return -1;
    u8 st = cmos(0x0B);
    int hh = h, pm = 0;
    if (!(st & 2)) {
        pm = h >= 12;
        hh = h % 12;
        if (hh == 0) hh = 12;
    }
    cmos_w(0x0B, st | 0x80);
    if (!(st & 4)) {
        cmos_w(0, tobcd(s)); cmos_w(2, tobcd(m));
        cmos_w(4, (u8)(tobcd(hh) | (pm ? 0x80 : 0)));
        cmos_w(7, tobcd(D)); cmos_w(8, tobcd(M)); cmos_w(9, tobcd(Y - 2000));
    } else {
        cmos_w(0, (u8)s); cmos_w(2, (u8)m);
        cmos_w(4, (u8)(hh | (pm ? 0x80 : 0)));
        cmos_w(7, (u8)D); cmos_w(8, (u8)M); cmos_w(9, (u8)(Y - 2000));
    }
    cmos_w(0x0B, st & (u8)~0x80);
    return 0;
}

void reboot(void)
{
    shutdown_run();
    cli();
    kbc_wait_in();
    outb(0x64, 0xFE);
    for (;;) hlt();
}
