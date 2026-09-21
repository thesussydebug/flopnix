/* Connects USB storage through a UHCI controller. */
#include "os.h"
#include "ioguard.inc"
#include "usbdebounce.inc"

#define USBCMD   0x00
#define USBSTS   0x02
#define USBINTR  0x04
#define FRNUM    0x06
#define FLBASE   0x08
#define SOFMOD   0x0C
#define PORTSC1  0x10

typedef struct __attribute__((aligned(16))) {
    volatile u32 link;
    volatile u32 cs;
    volatile u32 token;
    volatile u32 buf;
} TD;

typedef struct __attribute__((aligned(16))) {
    volatile u32 head;
    volatile u32 elem;
} QH;

#define TD_TERM     1
#define TD_QH       2
#define TD_VF       4
#define TD_ACTIVE   (1u << 23)
#define TD_STALLED  (1u << 22)
#define TD_DBUFERR  (1u << 21)
#define TD_BABBLE   (1u << 20)
#define TD_CRCTO    (1u << 18)
#define TD_BITSTUFF (1u << 17)
#define TD_HARDERR  (TD_STALLED | TD_DBUFERR | TD_BABBLE | TD_CRCTO | TD_BITSTUFF)
#define TD_CERR3    (3u << 27)

#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

static u16 io;
static int dev_ok;
static u8  dev_addr = 1;
static u8  ep_in, ep_out;
static u8  msc_iface;
static u16 mps_in = 64, mps_out = 64, mps0 = 8;
static u8  tog_in, tog_out;
static u32 dev_blocks, dev_bsize;
static char dev_model[36];

int usb_stage;
u32 usb_dbg_cs0, usb_dbg_cs1, usb_dbg_cs2;
u16 usb_dbg_wsts;
u32 usb_dbg_wedges;
u32 usb_dbg_resets;
static volatile u32 usb_gen_ctr;

static u32 framelist[1024] __attribute__((aligned(4096)));
static QH  qh;
static TD  td[16];
static u8  setup_buf[8];

static u32 pci_rd(u8 b, u8 d, u8 f, u8 o)
{
    outl(0xCF8, 0x80000000u | ((u32)b << 16) | ((u32)d << 11) | ((u32)f << 8) | (o & 0xFC));
    return inl(0xCFC);
}
static void pci_wr(u8 b, u8 d, u8 f, u8 o, u32 v)
{
    outl(0xCF8, 0x80000000u | ((u32)b << 16) | ((u32)d << 11) | ((u32)f << 8) | (o & 0xFC));
    outl(0xCFC, v);
}

static u16 find_uhci(void)
{
    for (u8 bus = 0; bus < 4; bus++)
        for (u8 dev = 0; dev < 32; dev++)
            for (u8 fn = 0; fn < 8; fn++) {
                u32 id = pci_rd(bus, dev, fn, 0);
                if (id == 0xFFFFFFFF) { if (fn == 0) break; else continue; }
                u32 cc = pci_rd(bus, dev, fn, 0x08) >> 8;
                if (cc == 0x0C0300) {
                    pci_wr(bus, dev, fn, 4, pci_rd(bus, dev, fn, 4) | 5);
                    pci_wr(bus, dev, fn, 0xC0, 0x8F00);
                    for (int bar = 0x10; bar <= 0x24; bar += 4) {
                        u32 b = pci_rd(bus, dev, fn, bar);
                        if (b & 1) return (u16)(b & ~3u);
                    }
                }
            }
    return 0;
}

static IoGuard usb_guard;

volatile int usb_quiet;

static void usb_yield(void) { if (!usb_quiet) gui_pump(); }

static void ms_delay(u32 ms)
{
    u32 t0 = ticks, guard = 0;
    u32 want = (ms + 9) / 10;
    u32 lim = timer_alive ? 200000000u : 2000000u;
    if (!want) want = 1;

    while ((u32)(ticks - t0) < want && ++guard < lim) usb_yield();
}

static void teardown_restart(void)
{
    u16 sts = inw(io + USBSTS);
    if (sts & 0x38) {
        usb_dbg_wsts = sts;
        usb_dbg_wedges++;
    }
    outw(io + USBCMD, inw(io + USBCMD) & ~1);
    for (int i = 0; i < 50000 && !(inw(io + USBSTS) & 0x20); i++) { }
    qh.elem = TD_TERM;
    qh.head = TD_TERM;
    outw(io + USBSTS, 0xFFFF);
    outl(io + FLBASE, (u32)framelist);
    outw(io + USBCMD, 0x00C1);
    ms_delay(2);
}

static int run_tds(int n)
{
    qh.elem = (u32)&td[0];
    qh.head = TD_TERM;

    u32 t0 = ticks, guard = 0;
    u32 lim = timer_alive ? 400000000u : 8000000u;
    for (;;) {
        int done = 1;
        int err = 0;
        for (int i = 0; i < n; i++) {
            u32 cs = td[i].cs;
            if (cs & TD_ACTIVE) { done = 0; break; }
            if (cs & TD_HARDERR) { err = 1; break; }
        }

        if (!(qh.elem & TD_TERM)) done = 0;
        if (err || (!done && ((u32)(ticks - t0) > 300 ||
                              ++guard > lim))) {
            usb_dbg_cs0 = td[0].cs;
            usb_dbg_cs1 = n > 1 ? td[1].cs : 0xEEEEEEEE;
            usb_dbg_cs2 = n > 2 ? td[2].cs : 0xEEEEEEEE;
            teardown_restart();
            return -1;
        }
        if (done) return 0;
        usb_yield();

    }
}

static void mk_td(int i, int last, u8 pid, u8 addr, u8 ep, u8 tog,
                  const void *buf, int len)
{
    td[i].link = last ? TD_TERM : ((u32)&td[i + 1] | TD_VF);
    td[i].cs = TD_ACTIVE | TD_CERR3;
    u32 ml = len ? ((u32)(len - 1) & 0x7FF) : 0x7FF;
    td[i].token = pid | ((u32)addr << 8) | ((u32)ep << 15) |
                  ((u32)tog << 19) | (ml << 21);
    td[i].buf = (u32)buf;
}

static int control(u8 addr, u8 rt, u8 req, u16 val, u16 idx,
                   void *buf, int len)
{
    setup_buf[0] = rt;
    setup_buf[1] = req;
    setup_buf[2] = val & 0xFF;
    setup_buf[3] = val >> 8;
    setup_buf[4] = idx & 0xFF;
    setup_buf[5] = idx >> 8;
    setup_buf[6] = len & 0xFF;
    setup_buf[7] = len >> 8;

    int n = 0, tog = 1;
    int in = (rt & 0x80) != 0;
    mk_td(n++, 0, PID_SETUP, addr, 0, 0, setup_buf, 8);

    int off = 0;
    while (off < len && n < 14) {
        int chunk = len - off > mps0 ? mps0 : len - off;
        mk_td(n++, 0, in ? PID_IN : PID_OUT, addr, 0, tog,
              (u8 *)buf + off, chunk);
        tog ^= 1;
        off += chunk;
    }
    mk_td(n++, 1, in ? PID_OUT : PID_IN, addr, 0, 1, 0, 0);
    return run_tds(n);
}

static void clear_halt(u8 ep, int in)
{
    control(dev_addr, 0x02, 1, 0, (in ? 0x80 : 0x00) | ep, 0, 0);
    if (in) tog_in = 0; else tog_out = 0;
}

static void bot_reset(void)
{
    usb_dbg_resets++;
    control(dev_addr, 0x21, 0xFF, 0, msc_iface, 0, 0);
    clear_halt(ep_in, 1);
    clear_halt(ep_out, 0);
    tog_in = tog_out = 0;
}

static int bulk(int in, void *buf, int len)
{
    u8 *tog = in ? &tog_in : &tog_out;
    u8 ep = in ? ep_in : ep_out;
    u16 mps = in ? mps_in : mps_out;

    int n = 0, off = 0;
    do {
        int chunk = len - off > mps ? mps : len - off;
        mk_td(n, off + chunk >= len, in ? PID_IN : PID_OUT,
              dev_addr, ep, *tog, (u8 *)buf + off, chunk);
        *tog ^= 1;
        off += chunk;
        n++;
    } while (off < len && n < 15);

    int r = run_tds(n);
    if (r != 0) {
        int stalled = 0;
        for (int i = 0; i < n; i++)
            if (td[i].cs & TD_STALLED) stalled = 1;
        if (stalled) clear_halt(ep, in);
    }
    return r;
}

static u32 cbw_tag = 1;

static int scsi(const u8 *cmd, int cmdlen, int in, u8 *data, u32 dlen)
{
    u8 cbw[31];
    memset(cbw, 0, 31);
    *(u32 *)cbw = 0x43425355;
    *(u32 *)(cbw + 4) = cbw_tag++;
    *(u32 *)(cbw + 8) = dlen;
    cbw[12] = in ? 0x80 : 0x00;
    cbw[13] = 0;
    cbw[14] = cmdlen;
    memcpy(cbw + 15, cmd, cmdlen);

    if (bulk(0, cbw, 31) != 0) { bot_reset(); return -1; }
    int data_err = 0;
    if (dlen) {
        u32 off = 0;
        while (off < dlen) {
            u32 chunk = dlen - off > 512 ? 512 : dlen - off;
            if (bulk(in, data + off, chunk) != 0) { data_err = 1; break; }
            off += chunk;
        }
    }
    u8 csw[13];
    if (bulk(1, csw, 13) != 0 && bulk(1, csw, 13) != 0) {
        bot_reset();
        return -1;
    }
    if (data_err) return -1;
    if (*(u32 *)csw != 0x53425355) { bot_reset(); return -1; }
    return csw[12];
}

static void request_sense(void)
{
    u8 cmd[16], sense[18];
    memset(cmd, 0, 16);
    cmd[0] = 0x03; cmd[4] = 18;
    scsi(cmd, 6, 1, sense, 18);
}

static int enumerate(void)
{
    usb_stage = 1;
    int found = 0;
    for (int p = 0; p < 2 && !found; p++) {
        u16 ps = PORTSC1 + p * 2;
        if (!(inw(io + ps) & 1)) continue;
        u16 base = inw(io + ps) & 0x00F5;
        outw(io + ps, base | 0x0200);
        ms_delay(50);
        outw(io + ps, base & ~0x0200);
        ms_delay(10);
        for (int tries = 0; tries < 10; tries++) {
            u16 b = inw(io + ps) & 0x00F5;
            outw(io + ps, b | 0x0004);
            ms_delay(10);
            if (inw(io + ps) & 0x0004) { found = 1; break; }
        }
    }
    if (!found) return 0;
    usb_stage = 2;
    ms_delay(100);

    u8 desc[96];

    mps0 = 8;
    if (control(0, 0x80, 6, 0x0100, 0, desc, 8) != 0) return 0;
    if (desc[7] == 16 || desc[7] == 32 || desc[7] == 64) mps0 = desc[7];
    if (control(0, 0x00, 5, dev_addr, 0, 0, 0) != 0) return 0;
    ms_delay(5);
    usb_stage = 3;
    if (control(dev_addr, 0x80, 6, 0x0100, 0, desc, 18) != 0) return 0;
    usb_stage = 4;

    if (control(dev_addr, 0x80, 6, 0x0200, 0, desc, 9) != 0) return 0;
    u16 total = desc[2] | (desc[3] << 8);
    if (total > sizeof desc) total = sizeof desc;
    if (control(dev_addr, 0x80, 6, 0x0200, 0, desc, total) != 0) return 0;
    usb_stage = 5;

    u8 cfg_val = desc[5];
    int is_msc = 0;
    ep_in = ep_out = 0;
    for (int i = 0; i + 2 <= total; ) {
        u8 dl = desc[i], dt = desc[i + 1];
        if (!dl) break;
        if (dt == 4 && i + 8 <= total) {
            is_msc = (desc[i + 5] == 0x08 && desc[i + 7] == 0x50);
            if (is_msc) msc_iface = desc[i + 2];
        }
        if (dt == 5 && is_msc && i + 7 <= total) {
            u8 ea = desc[i + 2], attr = desc[i + 3];
            u16 mp = desc[i + 4] | ((u16)desc[i + 5] << 8);
            if ((attr & 3) == 2 && mp >= 8 && mp <= 64) {
                if (ea & 0x80) { ep_in = ea & 0x0F; mps_in = mp; }
                else           { ep_out = ea & 0x0F; mps_out = mp; }
            }
        }
        i += dl;
    }
    if (!is_msc || !ep_in || !ep_out) return 0;
    usb_stage = 6;

    if (control(dev_addr, 0x00, 9, cfg_val, 0, 0, 0) != 0) return 0;
    ms_delay(10);
    tog_in = tog_out = 0;
    usb_stage = 7;

    u8 inq[36];
    u8 cmd[16];
    memset(cmd, 0, 16);
    cmd[0] = 0x12; cmd[4] = 36;
    if (scsi(cmd, 6, 1, inq, 36) == 0) {
        int k = 0;
        for (int j = 8; j < 36 && inq[j]; j++)
            if (k < (int)sizeof dev_model - 1) dev_model[k++] = inq[j];
        while (k && dev_model[k - 1] == ' ') k--;
        dev_model[k] = 0;
    } else {
        strlcpy(dev_model, "USB Storage", sizeof dev_model);
    }

    memset(cmd, 0, 16);
    for (int t = 0; t < 8; t++) {
        if (scsi(cmd, 6, 0, 0, 0) == 0) break;
        request_sense();
        ms_delay(100);
    }

    usb_stage = 8;
    u8 cap[8];
    for (int t = 0; t < 3; t++) {
        memset(cmd, 0, 16);
        cmd[0] = 0x25;
        if (scsi(cmd, 10, 1, cap, 8) == 0) goto gotcap;
        request_sense();
        ms_delay(50);
    }
    return 0;
gotcap:
    usb_stage = 9;
    dev_blocks = ((u32)cap[0] << 24) | (cap[1] << 16) | (cap[2] << 8) | cap[3];

    if (++dev_blocks == 0) return 0;
    dev_bsize  = ((u32)cap[4] << 24) | (cap[5] << 16) | (cap[6] << 8) | cap[7];
    if (dev_bsize != 512) return 0;
    return 1;
}

static void clear_port_changes(void)
{
    for (int p = 0; p < 2; p++) {
        u16 ps = PORTSC1 + p * 2;
        u16 s = inw(io + ps);
        outw(io + ps, (s & 0x00F5) | 0x000A);
    }
}

void usb_init(void)
{
    io = find_uhci();
    if (!io) return;
    bmark('1');

    outw(io + USBCMD, 0x0004);
    ms_delay(15);
    outw(io + USBCMD, 0x0000);
    ms_delay(5);
    outw(io + USBCMD, 0x0002);
    for (int i = 0; i < 100 && (inw(io + USBCMD) & 0x0002); i++) ms_delay(1);
    bmark('2');

    outw(io + USBINTR, 0);
    outw(io + FRNUM, 0);
    qh.head = TD_TERM;
    qh.elem = TD_TERM;
    for (int i = 0; i < 1024; i++) framelist[i] = (u32)&qh | TD_QH;
    outl(io + FLBASE, (u32)framelist);
    outb(io + SOFMOD, 0x40);
    outw(io + USBSTS, 0xFFFF);
    outw(io + USBCMD, 0x00C1);
    ms_delay(50);
    bmark('3');

    dev_ok = enumerate();
    clear_port_changes();
}

void usb_poll(void)
{
    if (!io) return;
    static u32 last;
    if (timer_alive) {
        if ((u32)(ticks - last) < 25) return;
        last = ticks;
    } else {
        static u32 cc;
        if (++cc & 0x3FFF) return;
    }

    static int last_connected = -1;
    static int pend = -1;
    static u32 pend_tick;

    int connected = 0;
    if (pend < 0) {

        int changed = 0;
        for (int p = 0; p < 2; p++)
            if (inw(io + PORTSC1 + p * 2) & 0x0002) changed = 1;
        if (!changed) return;
        for (int p = 0; p < 2; p++)
            if (inw(io + PORTSC1 + p * 2) & 0x0001) connected = 1;
    } else {
        for (int p = 0; p < 2; p++)
            if (inw(io + PORTSC1 + p * 2) & 0x0001) connected = 1;
    }

    int verdict;
    if (!timer_alive)

        verdict = connected == last_connected ? USB_DB_DROP : USB_DB_ACT;
    else
        verdict = usb_db_step(pend, pend_tick, connected, last_connected, ticks);
    if (verdict == USB_DB_ARM) {
        pend = connected;
        pend_tick = ticks;
        clear_port_changes();
        return;
    }
    if (verdict == USB_DB_NONE) return;
    pend = -1;
    if (verdict == USB_DB_DROP) {
        clear_port_changes();
        return;
    }
    last_connected = connected;

    if (connected) {

        if (!io_acquire(&usb_guard)) return;
        ms_delay(150);
        dev_ok = enumerate();
        if (!dev_ok) { ms_delay(100); dev_ok = enumerate(); }
        io_release(&usb_guard);
    } else {
        dev_ok = 0;
    }
    clear_port_changes();
    usb_gen_ctr++;
}

int usb_present(void) { return dev_ok; }
u32 usb_generation(void) { return usb_gen_ctr; }
const char *usb_model(void) { return dev_ok ? dev_model : "none"; }
u32 usb_capacity_kb(void) { return dev_ok ? dev_blocks / 2 : 0; }
u32 usb_capacity_sectors(void) { return dev_ok ? dev_blocks : 0; }

static int usb_read_inner(u32 lba, u32 count, u8 *buf)
{
    if (!dev_ok) return -1;
    if (!io_acquire(&usb_guard)) return -1;
    int rc = 0;
    while (count) {
        u32 n = count > 64 ? 64 : count;
        u8 cmd[16];
        memset(cmd, 0, 16);
        cmd[0] = 0x28;
        cmd[2] = lba >> 24; cmd[3] = lba >> 16; cmd[4] = lba >> 8; cmd[5] = lba;
        cmd[7] = n >> 8; cmd[8] = n & 0xFF;
        int r = scsi(cmd, 10, 1, buf, n * 512);
        if (r != 0) {
            request_sense();
            if (scsi(cmd, 10, 1, buf, n * 512) != 0) { rc = -1; break; }
        }
        app_note_io();
        lba += n; buf += n * 512; count -= n;
    }
    io_release(&usb_guard);
    return rc;
}

static int usb_write_inner(u32 lba, u32 count, const u8 *buf)
{
    if (!dev_ok) return -1;
    if (!io_acquire(&usb_guard)) return -1;
    int rc = 0;
    while (count) {
        u32 n = count > 64 ? 64 : count;
        u8 cmd[16];
        memset(cmd, 0, 16);
        cmd[0] = 0x2A;
        cmd[2] = lba >> 24; cmd[3] = lba >> 16; cmd[4] = lba >> 8; cmd[5] = lba;
        cmd[7] = n >> 8; cmd[8] = n & 0xFF;
        int r = scsi(cmd, 10, 0, (u8 *)buf, n * 512);
        if (r != 0) {
            request_sense();
            if (scsi(cmd, 10, 0, (u8 *)buf, n * 512) != 0) { rc = -1; break; }
        }
        app_note_io();
        lba += n; buf += n * 512; count -= n;
    }
    io_release(&usb_guard);
    return rc;
}

int usb_read(u32 lba,u32 count,u8 *buf)
{
    int r=usb_read_inner(lba,count,buf);debug_disk(1,0,lba,count,r);return r;
}
int usb_write(u32 lba,u32 count,const u8 *buf)
{
    int r=usb_write_inner(lba,count,buf);debug_disk(1,1,lba,count,r);return r;
}
