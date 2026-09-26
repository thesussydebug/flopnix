/* Runs the network adapters and IPv4 services. */
#include "kapi.h"
#include "debug.h"
#include "nettext.h"
#include "netlisten.h"
#include "http_core.inc"

static const Kapi *api;
static __attribute__((noinline)) void *net_copy(void *dst,const void *src,u32 n)
{
    volatile u8 *d=dst;const volatile u8 *s=src;
    while(n--)*d++=*s++;
    return dst;
}
static __attribute__((noinline)) void *net_fill(void *dst,int value,u32 n)
{
    volatile u8 *d=dst;while(n--)*d++=(u8)value;return dst;
}
static inline u8 net_inb(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u16 net_inw(u16 p){u16 v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static inline u32 net_inl(u16 p){u32 v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static inline void net_outb(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void net_outw(u16 p,u16 v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void net_outl(u16 p,u32 v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}


#define kfmt            api->kfmt
#define strlen          api->strlen
#define strcmp          api->strcmp
#define strncmp         api->strncmp
#define strcasecmp      api->strcasecmp
#define strlcpy         api->strlcpy
#define memcpy          net_copy
#define memmove         api->memmove
#define memset          net_fill
#define human_size      api->human_size
#define human_size_kb   api->human_size_kb
#define ticks           (*api->ticks)
#define timer_alive     (*api->timer_alive)
#define rtc_read        api->rtc_read
#define rtc_now_dos     api->rtc_now_dos
#define dos_fmt         api->dos_fmt
#define outb            net_outb
#define inb net_inb
#define outw            net_outw
#define inw net_inw
#define outl            net_outl
#define inl net_inl
#define CFG             (api->cfg)

void net_init(void);
void net_poll(void);
int  net_up(void);
int  net_dhcp(u32 timeout);
int  net_ping(u32 dst, u32 timeout);

static volatile int net_cancel;

static u32 net_irq_save(void){u32 f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;}
static void net_irq_restore(u32 f){__asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");}
static void net_wait(void)
{
    net_poll();
    if (api->gui_pump && api->gui_pump()) net_cancel = 1;
}

u8  net_mac[6];
u32 net_ip, net_gw, net_mask;
u8  net_dhcp_ok;
static u32 net_dns_srv;

enum { NIC_NONE, NIC_NE2K, NIC_RTL8139, NIC_TULIP, NIC_PCNET };
static int nic_kind;
static int pm_active;
static void nic_poll(void);
static u16 io;
static u16 rio;

#include "net_wire.inc"
#include "dhcp_core.inc"
#include "netprefs.inc"
#include "netconfig.inc"
static NetPrefs prefs;
static int ne_is_pci;
static u32 dns_second;
#include "net_tcp.inc"
#include "nicdesc.inc"
#include "phylink.inc"
#include "netq.inc"
#define HSQ 32768
static RxQueue rxq;
static DnsCache dnsc;
static int nic_irq=-1,rx_irq_on,rx_irq_stopped;
static u32 rx_mss=536,rx_irq_calls,rx_irq_idle,rx_irq_tick,rx_irq_burst,rx_irq_empty;
static void rx_irq_start(void);
static u32 rx_slots(void);

static u16 htons16(u16 v) { return (u16)((v << 8) | (v >> 8)); }

static u16 csum(const void *p, int n)
{
    const u16 *w = p;
    u32 s = 0;
    while (n > 1) { s += *w++; n -= 2; }
    if (n) s += *(const u8 *)w;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 off)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

static u32 found_pci_id;
static u8 found_pci_irq;
static u8 *nic_dma_alloc(u32 size,u32 align)
{
    u8 *p=api->kmalloc(size+align);if(!p)return 0;
    api->mem_track("NIC receive ring",p,size+align);
    return (u8 *)(((u32)p+align-1)&~(align-1));
}
static u16 find_pci_ids(const u32 *ids, int count, int master)
{
    for (int bus = 0; bus < 256; bus++)
        for (int dev = 0; dev < 32; dev++) {
            if ((pci_read(bus, dev, 0, 0) & 0xffff) == 0xffff) continue;
            int nf = pci_read(bus, dev, 0, 0x0c) & 0x00800000 ? 8 : 1;
            for (int fn = 0; fn < nf; fn++) {
                u32 id = pci_read(bus, dev, fn, 0);
                for (int n = 0; n < count; n++) {
                    if (id != ids[n]) continue;
                    for (int off = 0x10; off <= 0x24; off += 4) {
                        u32 bar = pci_read(bus, dev, fn, off);
                        if (!(bar & 1) || !(bar & ~3u) || (bar & ~3u) > 0xff00) continue;
                        u32 command = pci_read(bus, dev, fn, 4) & 0xffff;
                        pci_write(bus, dev, fn, 4, command | (master ? 5 : 1));
                        if (master) {
                            u32 v = pci_read(bus, dev, fn, 0x0c);
                            if ((v & 0xff00) < 0x1000) v = (v & ~0xff00u) | 0x4000;
                            if (!(v & 255)) v |= 8;
                            pci_write(bus, dev, fn, 0x0c, v);
                        }
                        found_pci_id = id;
                        found_pci_irq = (u8)pci_read(bus, dev, fn, 0x3c);
                        return (u16)(bar & ~3u);
                    }
                }
            }
        }
    return 0;
}
static u16 find_ne2k_pci(void)
{

    static const u32 ids[] = {0x802910ec, 0x09401050, 0x140111f6, 0x30008e2e,
        0x50004a14, 0x09261106, 0x0e3410bd, 0x5a5a1050};
    return find_pci_ids(ids, sizeof ids / sizeof ids[0], 0);
}
static u16 find_rtl8139(void)
{

    static const u32 ids[] = {0x813910ec, 0x13001186};
    return find_pci_ids(ids, 2, 1);
}

static u8  rtl_rx_static[8192 + 16 + 2048] __attribute__((aligned(32)));
static u8 *rtl_rx = rtl_rx_static;
static u32 rtl_ring = 8192;
static u8  rtl_txb[4][1792] __attribute__((aligned(4)));
static int rtl_txn;
static u16 rtl_rxoff;

static void rtl_init(void)
{
    outb(rio + 0x52, 0x00);
    outb(rio + 0x37, 0x10);
    for (int i = 0; i < 100000 && (inb(rio + 0x37) & 0x10); i++) ;
    for (int i = 0; i < 6; i++) net_mac[i] = inb(rio + i);
    outb(rio + 0x37, 0x0C);
    if (rtl_rx == rtl_rx_static) {
        u8 *big = nic_dma_alloc(32768 + 16 + 2048, 32);
        if (big) { rtl_rx = big; rtl_ring = 32768; }
    }
    outl(rio + 0x30, (u32)rtl_rx);
    outw(rio + 0x3C, rx_irq_on ? 0x0051 : 0);

    outl(rio + 0x44, (7u << 13) | (7u << 8) | 0x80 | 0x0A | (rtl_ring == 32768 ? 2u << 11 : 0));
    outl(rio + 0x40, 0x03000700);
    outw(rio + 0x3E, 0xFFFF);
    rtl_rxoff = 0;
    rtl_txn = 0;
}

static void rtl_tx(const u8 *fr, u16 len)
{
    if (len > 1792) return;
    u8 *b = rtl_txb[rtl_txn];
    memcpy(b, fr, len);
    while (len < 60) b[len++] = 0;
    outl(rio + 0x20 + rtl_txn * 4, (u32)b);
    outl(rio + 0x10 + rtl_txn * 4, len);
    for (int i = 0; i < 100000 &&
         !(inl(rio + 0x10 + rtl_txn * 4) & 0xA000); i++) ;
    rtl_txn = (rtl_txn + 1) & 3;
}

static void handle_frame(u8 *fr, u16 len);
static void rx_push(u8 *frame, u16 len)
{
    if (!rxq.buf) { handle_frame(frame, len); return; }
    if (len > RXQ_FRAME) return;
    u8 *slot = rxq_slot(&rxq);
    if (!slot) return;
    memcpy(slot, frame, len);
    rxq_commit(&rxq, len);
}

#include "pcnet.inc"

static u32 diag_rx, diag_tx;

static void rtl_poll_hw(void)
{
    for (int guard = 0; guard < 64 && !(inb(rio + 0x37) & 0x01); guard++) {
        u8 *p = rtl_rx + rtl_rxoff;
        u16 st   = (u16)(p[0] | ((u16)p[1] << 8));
        u16 rlen = (u16)(p[2] | ((u16)p[3] << 8));
        if (!(st & 1) || rlen < 18 || rlen > 1792) {
            rtl_init();
            return;
        }
        rx_push(p + 4, rlen - 4);
        rtl_rxoff = (u16)((rtl_rxoff + 4 + rlen + 3) & ~3u);
        if (rtl_rxoff >= rtl_ring) rtl_rxoff = (u16)(rtl_rxoff - rtl_ring);
        outw(rio + 0x38, (u16)(rtl_rxoff - 16));
        outw(rio + 0x3E, 0x01);
    }
}

#define T_CSR(n) (tul_io + (n) * 8)
#define TUL_NRX 8
#define TUL_BIG 32

typedef struct { volatile u32 status, length, buffer1, buffer2; } TulDesc;
static TulDesc tul_rxd_static[TUL_NRX] __attribute__((aligned(16)));
static TulDesc tul_txd[2]       __attribute__((aligned(16)));
static u8  tul_rxb_static[TUL_NRX][1600] __attribute__((aligned(4)));
static TulDesc *tul_rxd = tul_rxd_static;
static u8 *tul_rxb = &tul_rxb_static[0][0];
static int tul_nrx = TUL_NRX;
static u8  tul_txb[2][1600]       __attribute__((aligned(4)));
static u16 tul_io;
static int tul_rxi, tul_txi;
static int tul_admtek;
static int tul_full;
static int mac_is_fallback;

static u16 find_tulip(void)
{
    static const u32 ids[] = {
        0x09851317,
        0x09811317,
        0x00191011,
    };
    u16 base = find_pci_ids(ids, sizeof ids / sizeof ids[0], 1);
    tul_admtek = (found_pci_id & 0xffff) == 0x1317;
    return base;
}

static u16 tul_mii(int reg)
{
    if (!tul_admtek || reg < 0 || reg > 6) return 0xFFFF;
    return (u16)inl(tul_io + 0xB4 + (reg << 2));
}

static void tul_link(PhyState *p)
{
    if (nic_kind != NIC_TULIP || !tul_admtek) {
        p->present = 0; p->up = 0; p->autoneg_done = 0; p->full = 0; p->mbps = 0;
        return;
    }

    (void)tul_mii(1);
    phy_decode(tul_mii(1), tul_mii(4), tul_mii(5), tul_mii(0), p);
    if (!p->present) phy_admtek_decode(inl(tul_io + 0xFC), p);
}

static void tul_init(void)
{

    outl(T_CSR(6), 0x00040000);
    outl(T_CSR(0), 1);
    for (volatile int i = 0; i < 50000; i++) ;

    outl(T_CSR(0), 0x01A08000);

    if (tul_admtek) {
        u32 lo = inl(tul_io + 0xA4), hi = inl(tul_io + 0xA8);
        net_mac[0] = (u8)lo; net_mac[1] = (u8)(lo >> 8);
        net_mac[2] = (u8)(lo >> 16); net_mac[3] = (u8)(lo >> 24);
        net_mac[4] = (u8)hi; net_mac[5] = (u8)(hi >> 8);
    }
    int bad = 1;
    for (int i = 0; i < 6; i++) if (net_mac[i] != 0 && net_mac[i] != 0xFF) bad = 0;
    if (bad) {
        static const u8 fb[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
        for (int i = 0; i < 6; i++) net_mac[i] = fb[i];
        mac_is_fallback = 1;

    }
    if (tul_admtek) {
        outl(tul_io + 0xA4, (u32)net_mac[0] | ((u32)net_mac[1] << 8) |
             ((u32)net_mac[2] << 16) | ((u32)net_mac[3] << 24));
        outl(tul_io + 0xA8, (u32)net_mac[4] | ((u32)net_mac[5] << 8));
        outl(tul_io + 0xAC, 0);
        outl(tul_io + 0xB0, 0);
    }

    if (tul_rxd == tul_rxd_static) {
        u8 *big = nic_dma_alloc(TUL_BIG * (sizeof(TulDesc) + 1600), 16);
        if (big) { tul_rxd = (TulDesc *)big; tul_rxb = big + TUL_BIG * sizeof(TulDesc); tul_nrx = TUL_BIG; }
    }
    for (int i = 0; i < tul_nrx; i++) {
        tul_rxd[i].length  = td_rx_buf(1536, i == tul_nrx - 1);
        tul_rxd[i].buffer1 = (u32)(tul_rxb + i * 1600);
        tul_rxd[i].buffer2 = 0;
        tul_rxd[i].status  = TD_OWN;
    }
    for (int i = 0; i < 2; i++) {
        tul_txd[i].status = 0;
        tul_txd[i].length = 0;
        tul_txd[i].buffer1 = (u32)tul_txb[i];
        tul_txd[i].buffer2 = 0;
    }
    tul_rxi = tul_txi = 0;
    outl(T_CSR(3), (u32)tul_rxd);
    outl(T_CSR(4), (u32)tul_txd);
    outl(T_CSR(7), 0);
    outl(T_CSR(5), 0xFFFFFFFF);

    PhyState ph;
    tul_link(&ph);

    for (int i = 0; ph.present && !(ph.up && ph.autoneg_done) && i < 24; i++) {
        api->sleep_ms(50);
        tul_link(&ph);
    }
    tul_full = ph.full;

    u32 c6 = tul_csr6(tul_full, 0);
    outl(T_CSR(6), c6);
    outl(T_CSR(6), c6 | 0x2000);
    outl(T_CSR(6), tul_csr6(tul_full, 1));

    if (!tul_admtek) {

        u8 *sf = tul_txb[0];
        memset(sf, 0xFF, 192);
        u8 *e = sf + 15 * 12;
        for (int i = 0; i < 3; i++) {
            e[i * 4 + 0] = e[i * 4 + 2] = net_mac[i * 2];
            e[i * 4 + 1] = e[i * 4 + 3] = net_mac[i * 2 + 1];
        }
        tul_txd[0].length  = 0x08000000u | 192;
        tul_txd[0].buffer1 = (u32)sf;
        tul_txd[0].status  = TD_OWN;
        outl(T_CSR(1), 1);
        for (int i = 0; i < 400000 && (tul_txd[0].status & TD_OWN); i++) ;
        tul_txi = 1;
    }
    outl(T_CSR(2), 1);
}

static void tul_tx(const u8 *fr, u16 len)
{
    TulDesc *d = &tul_txd[tul_txi];
    for (int i = 0; i < 400000 && (d->status & TD_OWN); i++) ;
    if (d->status & TD_OWN) return;
    memcpy(tul_txb[tul_txi], fr, len);
    d->length = td_tx_len(len, tul_txi == 1);
    d->status = TD_OWN;
    outl(T_CSR(1), 1);
    tul_txi ^= 1;
}

static void tul_poll_hw(void)
{
    for (int guard = 0; guard < tul_nrx; guard++) {
        u32 st = tul_rxd[tul_rxi].status;
        if (st & TD_OWN) break;
        if (td_rx_ok(st))
            rx_push(tul_rxb + tul_rxi * 1600, (u16)td_rx_len(st));
        tul_rxd[tul_rxi].status = TD_OWN;
        tul_rxi = (tul_rxi + 1) % tul_nrx;
        outl(T_CSR(2), 1);
    }
}

#define CR    0x00
#define PSTART 0x01
#define PSTOP  0x02
#define BNRY   0x03
#define TPSR   0x04
#define TBCR0  0x05
#define TBCR1  0x06
#define ISR    0x07
#define RSAR0  0x08
#define RSAR1  0x09
#define RBCR0  0x0A
#define RBCR1  0x0B
#define RCR    0x0C
#define TCR    0x0D
#define DCR    0x0E
#define IMR    0x0F
#define DATA   0x10
#define NE_RESET 0x1F

#define PG_TX    0x40
#define PG_RSTART 0x46
#define PG_RSTOP  0x80

static int wait_isr(u8 bit)
{
    for (int i = 0; i < 100000; i++)
        if (inb(io + ISR) & bit) { outb(io + ISR, bit); return 1; }
    return 0;
}

static void rd_remote(u16 addr, u8 *buf, u16 len)
{
    u16 n = (len + 1) & ~1;
    outb(io + ISR, 0x40);
    outb(io + RBCR0, n & 0xFF);
    outb(io + RBCR1, n >> 8);
    outb(io + RSAR0, addr & 0xFF);
    outb(io + RSAR1, addr >> 8);
    outb(io + CR, 0x0A);
    for (u16 i = 0; i < n; i += 2) {
        u16 w = inw(io + DATA);
        buf[i] = w & 0xFF;
        if (i + 1 < len) buf[i + 1] = w >> 8;
    }
    wait_isr(0x40);
}

static void wr_remote(u16 addr, const u8 *buf, u16 len)
{
    u16 n = (len + 1) & ~1;
    outb(io + ISR, 0x40);
    outb(io + RBCR0, n & 0xFF);
    outb(io + RBCR1, n >> 8);
    outb(io + RSAR0, addr & 0xFF);
    outb(io + RSAR1, addr >> 8);
    outb(io + CR, 0x12);
    for (u16 i = 0; i < n; i += 2) {
        u16 w = buf[i] | ((u16)(i + 1 < len ? buf[i + 1] : 0) << 8);
        outw(io + DATA, w);
    }
    wait_isr(0x40);
}

static int ne2k_isa_probe(u16 base)
{
    u16 save = io;
    io = base;
    outb(io + NE_RESET, inb(io + NE_RESET));
    int ok = 0;
    for (int i = 0; i < 20000; i++)
        if (inb(io + ISR) & 0x80) { ok = 1; break; }
    if (ok) {
        outb(io + ISR, 0xFF);
        outb(io + CR, 0x21);
        outb(io + DCR, 0x49);
        u8 prom[32];
        rd_remote(0, prom, 32);
        ok = (prom[28] == 0x57 && prom[30] == 0x57);
    }
    io = save;
    return ok;
}

static void ne2k_init(void)
{
    outb(io + NE_RESET, inb(io + NE_RESET));
    for (int i = 0; i < 100000 && !(inb(io + ISR) & 0x80); i++) ;
    outb(io + ISR, 0xFF);

    outb(io + CR, 0x21);
    outb(io + DCR, 0x49);
    outb(io + RBCR0, 0); outb(io + RBCR1, 0);
    outb(io + IMR, 0);
    outb(io + ISR, 0xFF);
    outb(io + RCR, 0x20);
    outb(io + TCR, 0x02);

    u8 prom[32];
    rd_remote(0, prom, 32);
    for (int i = 0; i < 6; i++) net_mac[i] = prom[i * 2];

    outb(io + PSTART, PG_RSTART);
    outb(io + PSTOP, PG_RSTOP);
    outb(io + BNRY, PG_RSTART);
    outb(io + CR, 0x61);
    for (int i = 0; i < 6; i++) outb(io + 1 + i, net_mac[i]);
    outb(io + 7, PG_RSTART + 1);
    outb(io + CR, 0x21);
    outb(io + RCR, 0x04);
    outb(io + TCR, 0x00);
    outb(io + ISR, 0xFF);
    outb(io + CR, 0x22);
}

void net_init(void)
{
    net_ip = net_gw = net_mask = net_dns_srv = 0;
    net_dhcp_ok = 0;

    np_load(CFG, &prefs);
    dns_cache_init(&dnsc);
    rx_mss = tcp_mss_for_mtu(prefs.mtu);
    if (!rxq.buf) {
        u8 *store = api->kmalloc(RXQ_N * RXQ_FRAME);
        if (store) { api->mem_track("NIC receive queue", store, RXQ_N * RXQ_FRAME); rxq_init(&rxq, store); }
    }
    nic_kind = NIC_NONE; io = rio = tul_io = pc_io = 0;
    io = (!prefs.adapter || prefs.adapter == 1) ? find_ne2k_pci() : 0;
    ne_is_pci = io != 0;
    if (io) { nic_kind = NIC_NE2K; nic_irq = found_pci_irq; }
    if (!nic_kind) {
        rio = (!prefs.adapter || prefs.adapter == 2) ? find_rtl8139() : 0;
        if (rio) { nic_kind = NIC_RTL8139; nic_irq = found_pci_irq; }
    }
    if (!nic_kind) {
        tul_io = (!prefs.adapter || prefs.adapter == 3) ? find_tulip() : 0;
        if (tul_io) { nic_kind = NIC_TULIP; nic_irq = found_pci_irq; }
    }
    if (!nic_kind) {
        static const u32 ids[] = {0x20001022};
        pc_io = (!prefs.adapter || prefs.adapter == 4) ? find_pci_ids(ids, 1, 1) : 0;
        if (pc_io && pc_init()) { nic_kind = NIC_PCNET; nic_irq = found_pci_irq; }
    }
    if (!nic_kind && (!prefs.adapter || prefs.adapter == 5)) {

        if (prefs.isa_io) { if (ne2k_isa_probe(prefs.isa_io)) io = prefs.isa_io; }
        else if (ne2k_isa_probe(0x300)) io = 0x300;
        else if (ne2k_isa_probe(0x280)) io = 0x280;
        if (io) nic_kind = NIC_NE2K;
    }
    if (nic_kind == NIC_NE2K) ne2k_init();
    else if (nic_kind == NIC_RTL8139) rtl_init();
    else if (nic_kind == NIC_TULIP) tul_init();
    else if (nic_kind != NIC_PCNET) return;
    rx_irq_start();

    if (CFG->net_mode == 1) {
        if (!nc_validate(CFG->ip, CFG->mask, CFG->gw, prefs.dns, prefs.dns2)) {
            net_ip = CFG->ip; net_mask = CFG->mask; net_gw = CFG->gw;
            net_dns_srv = prefs.dns; dns_second = prefs.dns2;
        }
    } else {
        net_dhcp(1200);
    }
}

int net_up(void) { return nic_kind != NIC_NONE; }

static void capture_frame(const u8 *frame,u32 size)
{
    if(pm_active)return;
    static const DebugOps *d;
    if(!d)d=api->service_get("debug");
    if(d&&d->abi==DEBUG_ABI&&(d->flags&DBG_NET))d->packet(frame,size);
}

static void eth_send(const u8 *dst, u16 type, const u8 *payload, u16 plen)
{
    u8 fr[1536];
    u16 len = 14 + plen;
    if (len > 1514) return;
    diag_tx++;
    memcpy(fr, dst, 6);
    memcpy(fr + 6, net_mac, 6);
    fr[12] = type >> 8;
    fr[13] = type & 0xFF;
    memcpy(fr + 14, payload, plen);
    while (len < 60) fr[len++] = 0;

    u32 f=net_irq_save();
    capture_frame(fr,len);
    if (nic_kind == NIC_RTL8139) rtl_tx(fr, len);
    else if (nic_kind == NIC_TULIP) tul_tx(fr, len);
    else if (nic_kind == NIC_PCNET) pc_tx(fr, len);
    else {
        for (int i = 0; i < 100000 && (inb(io + CR) & 0x04); i++) ;
        wr_remote((u16)PG_TX << 8, fr, len);
        outb(io + TPSR, PG_TX);
        outb(io + TBCR0, len & 0xFF);
        outb(io + TBCR1, len >> 8);
        outb(io + CR, 0x26);
    }
    net_irq_restore(f);
}

typedef struct __attribute__((packed)) {
    u16 htype, ptype;
    u8  hlen, plen;
    u16 op;
    u8  sha[6]; u32 spa;
    u8  tha[6]; u32 tpa;
} Arp;

static struct { u32 ip, seen; u8 mac[6]; u8 valid; } arpc[4];
static int arpc_next;

static int arp_count(void)
{
    int n = 0;
    for (unsigned i = 0; i < sizeof arpc / sizeof arpc[0]; i++)
        if (arpc[i].valid) n++;
    return n;
}

static int arp_lookup(u32 ip, u8 *mac)
{
    for (int i = 0; i < 4; i++)
        if (arpc[i].valid && arpc[i].ip == ip && (u32)(ticks - arpc[i].seen) < 12000) {
            memcpy(mac, arpc[i].mac, 6);
            return 1;
        }
    return 0;
}

static void arp_store(u32 ip, const u8 *mac)
{
    for (int i = 0; i < 4; i++)
        if (arpc[i].valid && arpc[i].ip == ip) {
            arpc[i].seen = ticks;
            memcpy(arpc[i].mac, mac, 6);
            return;
        }
    arpc[arpc_next].ip = ip; arpc[arpc_next].seen = ticks;
    memcpy(arpc[arpc_next].mac, mac, 6);
    arpc[arpc_next].valid = 1;
    arpc_next = (arpc_next + 1) & 3;
}

static void arp_send(u16 op, const u8 *tha, u32 tpa)
{
    Arp a;
    a.htype = htons16(1);
    a.ptype = htons16(0x0800);
    a.hlen = 6; a.plen = 4;
    a.op = htons16(op);
    memcpy(a.sha, net_mac, 6);
    a.spa = net_ip;
    memcpy(a.tha, tha, 6);
    a.tpa = tpa;
    static const u8 bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    eth_send(op == 1 ? bcast : tha, 0x0806, (u8 *)&a, sizeof a);
}

typedef struct __attribute__((packed)) {
    u8  vihl, tos;
    u16 totlen, id, frag;
    u8  ttl, proto;
    u16 csum;
    u32 src, dst;
} Ip;

typedef struct __attribute__((packed)) {
    u8  type, code;
    u16 csum, id, seq;
} Icmp;

static u16 ping_seq;
static volatile u8 ping_got;
static u32 ping_target;

static void ip_send(u32 dst, const u8 *dmac, u8 proto, u8 *payload, u16 plen)
{
    u8 pkt[1500];
    if (plen > 1480 || 20u + plen > prefs.mtu) return;
    Ip *ip = (Ip *)pkt;
    ip->vihl = 0x45; ip->tos = 0;
    ip->totlen = htons16(20 + plen);
    ip->id = htons16(ping_seq);
    ip->frag = 0;
    ip->ttl = 64; ip->proto = proto;
    ip->csum = 0;
    ip->src = net_ip; ip->dst = dst;
    ip->csum = csum(ip, 20);
    memcpy(pkt + 20, payload, plen);
    eth_send(dmac, 0x0800, pkt, 20 + plen);
}

static void udp_send(u32 dst, const u8 *dmac, u16 sport, u16 dport,
                     const u8 *pay, u16 plen)
{
    u8 pkt[600];
    u16 ulen = 8 + plen;
    if (plen > (u16)(sizeof pkt - 8)) return;
    pkt[0] = (u8)(sport >> 8); pkt[1] = (u8)sport;
    pkt[2] = (u8)(dport >> 8); pkt[3] = (u8)dport;
    pkt[4] = (u8)(ulen >> 8);  pkt[5] = (u8)ulen;
    pkt[6] = 0; pkt[7] = 0;
    memcpy(pkt + 8, pay, plen);
    ip_send(dst, dmac, 17, pkt, ulen);
}

#include "faultnet.inc"
#include "panicnet.inc"

#define DNS_SPORT 1077
static u16 dns_id_cur;
static volatile u32 dns_answer;

static int arp_resolve(u32 ip, u8 *mac, u32 timeout);

#define SNTP_SPORT 1123
static volatile u32 sntp_answer;
static int arp_resolve(u32 ip, u8 *mac, u32 timeout);

static u32 net_sntp_impl_locked(u32 ip, u32 timeout)
{
    if (nic_kind == NIC_NONE || !timer_alive || !ip) return 0;
    net_cancel = 0;
    u32 hop = ((ip & net_mask) == (net_ip & net_mask)) ? ip : net_gw;
    u8 mac[6];
    if (!arp_resolve(hop, mac, timeout)) return 0;
    u8 q[48];
    memset(q, 0, sizeof q);
    q[0] = 0x1B;
    sntp_answer = 0;
    for (int tries = 0; tries < 3 && !sntp_answer && !net_cancel; tries++) {
        udp_send(ip, mac, SNTP_SPORT, 123, q, 48);
        u32 t0 = ticks;
        while (!sntp_answer && !net_cancel && (u32)(ticks - t0) < timeout / 3 + 1)
            net_wait();
    }
    return sntp_answer;
}
static u32 net_sntp_impl(u32 ip,u32 timeout){api->network_lock();u32 result=net_sntp_impl_locked(ip,timeout);api->network_unlock();return result;}


static u32 dns_expected;
static u32 net_dns_resolve_locked(const char *name, u32 timeout)
{
    u32 ip;
    if (nw_ip_parse(name, &ip)) return ip;
    ip = dns_cache_get(&dnsc, name, ticks);
    if (ip) return ip;
    if (nic_kind == NIC_NONE || !timer_alive || !net_ip || (!net_dns_srv && !dns_second)) return 0;
    net_cancel = 0; dns_answer = 0;
    u32 started = ticks;
    u32 servers[2] = {net_dns_srv, dns_second};
    for (int server = 0; server < 2 && !net_cancel; server++) {
        u32 srv = servers[server];
        if (!srv || (server && srv == servers[0]) || (u32)(ticks - started) >= timeout) continue;
        u32 hop = ((srv & net_mask) == (net_ip & net_mask)) ? srv : net_gw;
        u8 mac[6];
        if (!arp_resolve(hop, mac, timeout / 4 + 1)) continue;
        dns_expected = srv; dns_id_cur = (u16)(api->rand() ^ ticks);
        u8 q[300]; int ql = nw_dns_build(name, dns_id_cur, q, sizeof q);
        if (!ql) return 0;
        for (int tries = 0; tries < 2 && !dns_answer && !net_cancel; tries++) {
            if ((u32)(ticks - started) >= timeout) break;
            udp_send(srv, mac, DNS_SPORT, 53, q, (u16)ql);
            u32 t0 = ticks;
            while (!dns_answer && !net_cancel && (u32)(ticks - t0) < timeout / 4 + 1 &&
                   (u32)(ticks - started) < timeout) net_wait();
        }
        if (dns_answer) break;
    }
    dns_expected = 0;
    dns_cache_put(&dnsc, name, dns_answer, ticks, 30000);
    return dns_answer;
}
static u32 net_dns_resolve(const char *name,u32 timeout){api->network_lock();u32 result=net_dns_resolve_locked(name,timeout);api->network_unlock();return result;}


static Tcb tcb;
static u32 tcp_rip;
static u16 tcp_rport, tcp_lport;
static u8  tcp_rmac[6];
static const u8 *txq;
static int txq_len;
static u32 txq_seq;
static void (*tcp_deliver)(const u8 *d, int n);
static volatile int tcp_event;
static volatile u32 hs_read,hs_write;
static struct { u32 ip, since; u16 port; u8 open; char host[128]; } ka;

static void tcp_out(int flags, u32 seq, const u8 *pay, int plen)
{
    u8 seg[824];
    int hl = (flags & TCP_SYN) ? 24 : 20;
    if (plen > (int)sizeof seg - hl) return;
    seg[0] = (u8)(tcp_lport >> 8); seg[1] = (u8)tcp_lport;
    seg[2] = (u8)(tcp_rport >> 8); seg[3] = (u8)tcp_rport;
    seg[4] = (u8)(seq >> 24); seg[5] = (u8)(seq >> 16);
    seg[6] = (u8)(seq >> 8);  seg[7] = (u8)seq;
    u32 ack = (flags & TCP_ACK) ? tcb.rcv_nxt : 0;
    seg[8] = (u8)(ack >> 24); seg[9] = (u8)(ack >> 16);
    seg[10] = (u8)(ack >> 8); seg[11] = (u8)ack;
    seg[12] = (u8)(hl << 2);
    seg[13] = (u8)flags;
    u32 used = hs_write - hs_read;
    u32 window = tcp_rx_window(used > HSQ ? 0 : HSQ - used, rx_slots(), rx_mss);
    seg[14]=(u8)(window>>8);seg[15]=(u8)window;
    seg[16] = seg[17] = 0;
    seg[18] = seg[19] = 0;
    if (hl == 24) { seg[20] = 2; seg[21] = 4; seg[22] = (u8)(rx_mss >> 8); seg[23] = (u8)rx_mss; }
    if (plen) memcpy(seg + hl, pay, plen);
    u16 c = nw_tcp_csum(net_ip, tcp_rip, seg, hl + plen);
    seg[16] = (u8)(c >> 8); seg[17] = (u8)c;
    ip_send(tcp_rip, tcp_rmac, 6, seg, (u16)(hl + plen));
}

static void tcp_act(int a, const u8 *pay, int off, int len)
{
    if ((a & TA_DELIVER) && tcp_deliver) tcp_deliver(pay + off, len);
    if (a & TA_SEND_SYN) tcp_out(TCP_SYN, tcb.snd_una, 0, 0);
    if (a & TA_SEND_FIN) tcp_out(TCP_FIN | TCP_ACK, tcb.snd_nxt - 1, 0, 0);
    if (a & TA_SEND_DATA) {
        u32 doff = tcb.snd_una - txq_seq;
        if (txq && doff < (u32)txq_len)
            tcp_out(TCP_ACK | TCP_PSH, tcb.snd_una, txq + doff,
                    (int)(tcb.snd_nxt-tcb.snd_una));
    }
    if (a & TA_SEND_ACK) tcp_out(TCP_ACK, tcb.snd_nxt, 0, 0);
    tcp_event |= a & (TA_CONNECTED | TA_CLOSED | TA_ERROR);
}

static void ka_drop(void)
{
    if (ka.open && tcb.state != TS_CLOSED && tcb.state != TS_TIME_WAIT) tcp_out(TCP_RST | TCP_ACK, tcb.snd_nxt, 0, 0);
    if (ka.open) tcb.state = TS_CLOSED;
    ka.open = 0;
}

static void tcp_pump(void)
{
    if (ka.open && (tcb.state != TS_ESTAB || (u32)(ticks - ka.since) > 500)) ka_drop();
    if (tcb.state == TS_CLOSED || tcb.state == TS_TIME_WAIT) return;
    int a = tcp_tick(&tcb, ticks);
    if (a) tcp_act(a, 0, 0, 0);
}

#include "net_listen.inc"

static int (*hs_sink)(const u8 *, int, void *);
static void *hs_ctx;
static HttpReader hs_http;
static int hs_raw,hs_abort,hs_info_sent;
static NetHttpInfo *hs_info;
static u32 hs_got;
static volatile u8 tcp_busy;
#include "netpush.inc"
static NetHttpDiag http_diag;
static void http_diag_read(NetHttpDiag *out){out->stage=http_diag.stage;out->result=http_diag.result;}
static const NetHttpDiagOps http_diag_ops={NET_HTTP_DIAG_ABI,http_diag_read};

static u8 *hs_queue;
static void http_queue(const u8 *d,int n)
{
    if(hs_abort)return;
    u32 w=hs_write;
    if(n<0||(u32)n>HSQ-(w-hs_read)){hs_abort=1;return;}
    for(int i=0;i<n;i++)hs_queue[(w+(u32)i)&(HSQ-1)]=d[i];
    __asm__ volatile("" ::: "memory");hs_write=w+(u32)n;
}
static void http_drain(void)
{
    u8 chunk[512];u32 before=hs_read;
    while(hs_read!=hs_write&&!hs_abort){
        u32 r=hs_read,n=hs_write-r;if(n>sizeof chunk)n=sizeof chunk;
        for(u32 i=0;i<n;i++)chunk[i]=hs_queue[(r+i)&(HSQ-1)];
        __asm__ volatile("" ::: "memory");hs_read=r+n;
        if(hs_raw){if(hs_sink&&!hs_sink(chunk,(int)n,hs_ctx))hs_abort=1;}
        else if(!hr_feed(&hs_http,chunk,(int)n,hs_sink,hs_ctx))hs_abort=1;
        if(hs_info&&!hs_info_sent&&!hs_raw&&hs_http.state){memcpy(hs_info,&hs_http.info,sizeof *hs_info);hs_info_sent=1;}
    }

    u32 flags=net_irq_save();
    if(hs_read!=before&&tcb.state==TS_ESTAB)
        tcp_out(TCP_ACK,tcb.snd_nxt,0,0);
    net_irq_restore(flags);
}

static void http_deliver(const u8 *d, int n)
{
    if(hs_abort)return;
    hs_got+=(u32)n;http_queue(d,n);
}

static int net_transfer_locked(u32 ip, u16 port, const char *host,
                             const char *path,
                             int (*sink)(const u8 *, int, void *), void *ctx,
                             u32 timeout,NetHttpInfo *info,const char *body)
{
    if (nic_kind == NIC_NONE || !net_ip) return -2;
    if (!timer_alive || !port || !ip || !path || !timeout) return -1;
    u8 held=1;__asm__ volatile("xchgb %0,%1" : "+q"(held), "+m"(tcp_busy) :: "memory");
    if(held)return -5;
    int result=-3;
    http_diag.stage=NH_PREFLIGHT;http_diag.result=0;
    hs_queue=api->kmalloc(HSQ);if(!hs_queue){http_diag.result=-4;tcp_busy=0;return -4;}
    api->mem_track("HTTP receive queue",hs_queue,HSQ);
    static char req[5120];
    if(strlen(path)>500 || (host&&strlen(host)>127)) {result=-1;goto finish;}
    if(host){
        for(const char *p=host;*p;p++)if((u8)*p<=32||*p==127){result=-1;goto finish;}
        for(const char *p=path;*p;p++)if((u8)*p<=32||*p==127){result=-1;goto finish;}
        if(strlen(path)+strlen(host)+200+(body?strlen(body):0)>=sizeof req){result=-1;goto finish;}
        if(body)kfmt(req,sizeof req,"POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept-Encoding: identity\r\nUser-Agent: FLOPNIX\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %u\r\n\r\n%s",path,host,strlen(body),body);
        else kfmt(req,sizeof req,"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\nAccept-Encoding: identity\r\nUser-Agent: FLOPNIX\r\n\r\n",path,host);
    }else strlcpy(req,path,sizeof req);
    net_cancel = 0;
    api->esc_arm();
    int keep=0;
    for(int attempt=0;;attempt++){
    u32 critical=net_irq_save();
    int reuse=!attempt&&host&&!body&&ka.open&&ka.ip==ip&&ka.port==port&&!strcmp(ka.host,host)&&tcb.state==TS_ESTAB;
    if(!reuse)ka_drop();
    ka.open=0;
    net_irq_restore(critical);
    if(!reuse){
        u32 hop = ((ip & net_mask) == (net_ip & net_mask)) ? ip : net_gw;
        http_diag.stage=NH_ARP;
        if (!arp_resolve(hop, tcp_rmac, timeout)) goto finish;
        http_diag.stage=NH_CONNECT;
    }
    critical=net_irq_save();
    if(!reuse){
        tcp_rip = ip;
        tcp_rport = port;
        tcp_lport = (u16)(0xC000 | (ticks & 0x3FFF));
        if(tcp_lport==nl.port)tcp_lport=(u16)(0xC000|((tcp_lport+1)&0x3FFF));
    }
    hs_sink = sink; hs_ctx = ctx;
    hr_init(&hs_http);hs_raw=!host;hs_abort=0;hs_info=info;hs_info_sent=0;
    hs_read=hs_write=0;
    hs_got = 0;
    tcp_deliver = http_deliver;
    tcp_event = 0;

    if(!reuse)tcp_act(tcp_open(&tcb, ticks ^ 0x464C4F50u, ticks), 0, 0, 0);
    net_irq_restore(critical);
    u32 t0 = ticks;
    while (!reuse && !(tcp_event & (TA_CONNECTED | TA_ERROR))) {
        if (net_cancel || (u32)(ticks - t0) > timeout) { tcb.state = TS_CLOSED; goto fail3; }
        net_wait();
    }
    if (tcp_event & TA_ERROR) goto fail3;
    http_diag.stage=NH_RESPONSE;

    critical=net_irq_save();
    txq = (const u8 *)req;
    txq_len = (int)strlen(req);
    txq_seq = tcb.snd_nxt;
    int sent=txq_len>800?800:txq_len;
    tcp_out(TCP_ACK | TCP_PSH, tcb.snd_nxt, txq, sent);
    tcp_mark_sent(&tcb, sent, ticks);
    net_irq_restore(critical);

    t0 = ticks;
    u32 seen = hs_got;
    while (!(tcp_event & (TA_CLOSED | TA_ERROR)) && !hs_abort) {
        http_drain();
        if(!hs_raw&&hs_http.state==7)break;
        if (hs_got != seen) { seen = hs_got; t0 = ticks; }
        if (net_cancel || api->esc_pending() || (u32)(ticks - t0) > timeout) { hs_abort=1; break; }
        net_wait();
        critical=net_irq_save();
        if(tcb.snd_una==tcb.snd_nxt&&sent<txq_len&&tcb.state==TS_ESTAB){int n=txq_len-sent;if(n>800)n=800;tcp_out(TCP_ACK|TCP_PSH,tcb.snd_nxt,txq+sent,n);tcp_mark_sent(&tcb,n,ticks);sent+=n;t0=ticks;}
        if (tcb.state == TS_CLOSE_WAIT)
            tcp_act(tcp_close(&tcb, ticks), 0, 0, 0);
        net_irq_restore(critical);
    }
    http_drain();
    if(reuse&&!hs_got&&!net_cancel&&!api->esc_pending()&&(hs_abort||(tcp_event&(TA_CLOSED|TA_ERROR)))){
        critical=net_irq_save();
        if(tcb.state!=TS_CLOSED&&tcb.state!=TS_TIME_WAIT)tcp_out(TCP_RST|TCP_ACK,tcb.snd_nxt,0,0);
        tcb.state=TS_CLOSED;
        net_irq_restore(critical);
        continue;
    }
    break;
    }
    keep=host&&!body&&!hs_abort&&!(tcp_event&(TA_CLOSED|TA_ERROR))&&hr_reusable(&hs_http)&&tcb.state==TS_ESTAB;

    if(info)memcpy(info,&hs_http.info,sizeof *info);
    result=hs_abort||(tcp_event&TA_ERROR)||(!hs_raw&&!hr_finish(&hs_http))?(hs_http.error?hs_http.error:-4):hs_raw?0:hs_http.info.status;
    goto finish;
fail3:
    result=-3;
finish:
    http_diag.result=result;if(result>=0)http_diag.stage=NH_COMPLETE;
    ;u32 finish_flags=net_irq_save();
    if(result>=0&&keep){ka.open=1;ka.ip=ip;ka.port=port;ka.since=ticks;strlcpy(ka.host,host,sizeof ka.host);}
    else {
        if(tcb.state!=TS_CLOSED&&tcb.state!=TS_TIME_WAIT)tcp_out(TCP_RST|TCP_ACK,tcb.snd_nxt,0,0);
        tcb.state=TS_CLOSED;
    }
    tcp_deliver = 0;
    txq = 0;
    hs_sink=0;hs_ctx=0;hs_info=0;
    net_irq_restore(finish_flags);
    api->kfree(hs_queue);hs_queue=0;tcp_busy=0;
    return result;
}
static int net_transfer(u32 ip,u16 port,const char *host,const char *path,int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout,NetHttpInfo *info){api->network_lock();int result=net_transfer_locked(ip,port,host,path,sink,ctx,timeout,info,0);api->network_unlock();return result;}


static int net_http_get_impl(u32 ip,u16 port,const char *host,const char *path,
    int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout)
{ return net_transfer(ip,port,host,path,sink,ctx,timeout,0); }
static int net_http_get_info(u32 ip,u16 port,const char *host,const char *path,
    int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout,NetHttpInfo *info)
{
    if(info)memset(info,0,sizeof *info);
    if(!host)return -1;
    return net_transfer(ip,port,host,path,sink,ctx,timeout,info);
}
static int net_text_request(u32 ip,u16 port,const char *request,
    int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout)
{ return net_transfer(ip,port,0,request,sink,ctx,timeout,0); }
static const NetTextOps text_ops={NET_TEXT_ABI,net_text_request};
static const NetHttpOps http_ops={NET_HTTP_ABI,net_http_get_info};
static int net_http_post(u32 ip,u16 port,const char *host,const char *path,const char *body,int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout,NetHttpInfo *info)
{
    if(!host||!body||strlen(body)>4096)return -1;if(info)memset(info,0,sizeof *info);
    api->network_lock();int result=net_transfer_locked(ip,port,host,path,sink,ctx,timeout,info,body);api->network_unlock();return result;
}
static const NetHttpFormOps http_form_ops={NET_HTTP_FORM_ABI,net_http_post};

static u32 dhcp_xid, dhcp_serial, dhcp_started, dhcp_due;
static int dhcp_state, dhcp_auto, dhcp_busy, dhcp_attempts;
static DhReply dhcp_cfg;
static u32 dhcp_age, dhcp_tick, dhcp_fraction;
static u32 dhcp_offers, dhcp_acks, dhcp_naks, dhcp_rejected;

static void dhcp_clear_address(void)
{
    net_ip = net_mask = net_gw = net_dns_srv = dns_second = 0;
    net_dhcp_ok = 0;
    dns_cache_init(&dnsc);
    for (int i = 0; i < 4; i++) arpc[i].valid = 0;
    tcb.state = TS_CLOSED;
    tcp_event |= TA_ERROR;
}

static void dhcp_begin(void)
{
    memset(&dhcp_cfg, 0, sizeof dhcp_cfg);
    dhcp_xid = api->rand() ^ ticks ^ ++dhcp_serial;
    dhcp_started = dhcp_due = ticks;
    dhcp_state = DH_SELECT;
    dhcp_attempts = 0;
}

static int dhcp_send(void)
{
    u8 pkt[400];
    memset(pkt, 0, sizeof pkt);
    Ip *ip = (Ip *)pkt;
    u8 *udp = pkt + 20;
    int len = dh_build(udp + 8, sizeof pkt - 28, dhcp_state, dhcp_xid, net_mac,
                       dhcp_cfg.ip, dhcp_cfg.server, (ticks - dhcp_started) / 100);
    len = dh_hostname(udp + 8, len, sizeof pkt - 28, prefs.hostname[0] ? prefs.hostname : "flopnix");
    if (!len) return 0;
    u8 dest[6] = {255,255,255,255,255,255};
    u32 hop = (dhcp_cfg.server & net_mask) == (net_ip & net_mask)
              ? dhcp_cfg.server : net_gw;
    if (dhcp_state == DH_RENEW && (!hop || !arp_lookup(hop, dest))) {
        static const u8 zero[6] = {0,0,0,0,0,0};
        if (hop) arp_send(1, zero, hop);
        return 0;
    }
    u16 ulen = (u16)(8 + len), total = (u16)(20 + ulen);
    udp[1] = 68; udp[3] = 67;
    udp[4] = (u8)(ulen >> 8); udp[5] = (u8)ulen;
    ip->vihl = 0x45; ip->totlen = htons16(total);
    ip->ttl = 64; ip->proto = 17;
    ip->src = (dhcp_state == DH_RENEW || dhcp_state == DH_REBIND) ? net_ip : 0;
    ip->dst = dhcp_state == DH_RENEW ? dhcp_cfg.server : 0xffffffff;
    ip->csum = csum(ip, 20);
    eth_send(dest, 0x0800, pkt, total);
    return 1;
}

static void dhcp_recv(const u8 *p, u16 len)
{
    DhReply r;
    if (!dhcp_auto || dhcp_state == DH_OFF || dhcp_state == DH_BOUND) return;
    if (!dh_parse(p, len, dhcp_xid, net_mac, &r) ||
        !dh_accept(dhcp_state, dhcp_cfg.ip, dhcp_cfg.server, &r)) {
        dhcp_rejected++;
        return;
    }
    if (r.type == 6) {
        dhcp_naks++;
        dhcp_clear_address();
        dhcp_state = DH_OFF;
        dhcp_due = ticks + 1000;
        return;
    }
    if (r.type == 2) {
        dhcp_offers++;
        memcpy(&dhcp_cfg, &r, sizeof r);
        dhcp_state = DH_REQUEST;
        dhcp_attempts = 0;
        dhcp_due = ticks;
        return;
    }

    if (r.seen & DH_MASK) dhcp_cfg.mask = r.mask;
    if (r.seen & DH_GW) dhcp_cfg.gw = r.gw;
    if (r.seen & DH_DNS) { dhcp_cfg.dns = r.dns; dhcp_cfg.dns2 = r.dns2; }
    if (nc_validate(dhcp_cfg.ip, dhcp_cfg.mask, dhcp_cfg.gw, dhcp_cfg.dns, dhcp_cfg.dns2)) { dhcp_rejected++; return; }
    dhcp_cfg.server = r.server;
    dhcp_cfg.lease = r.lease;
    dhcp_cfg.t1 = (r.seen & DH_T1) ? r.t1 : r.lease / 2;
    dhcp_cfg.t2 = (r.seen & DH_T2) ? r.t2 : r.lease - r.lease / 8;
    if (!dhcp_cfg.t1 || dhcp_cfg.t1 >= dhcp_cfg.t2 || dhcp_cfg.t2 >= r.lease) {
        dhcp_cfg.t1 = r.lease / 2;
        dhcp_cfg.t2 = r.lease - r.lease / 8;
        if (dhcp_cfg.t2 >= r.lease) dhcp_cfg.t2 = r.lease - 1;
    }
    if (net_ip != dhcp_cfg.ip || net_gw != dhcp_cfg.gw || net_mask != dhcp_cfg.mask)
        for (int i = 0; i < 4; i++) arpc[i].valid = 0;
    net_ip = dhcp_cfg.ip; net_mask = dhcp_cfg.mask;
    net_gw = dhcp_cfg.gw; net_dns_srv = prefs.dns_manual ? prefs.dns : dhcp_cfg.dns;
    dns_second = prefs.dns_manual ? prefs.dns2 : dhcp_cfg.dns2;
    net_dhcp_ok = 1;
    dhcp_age = dhcp_fraction = 0;
    dhcp_tick = ticks;
    dhcp_state = DH_BOUND;
    dhcp_acks++;
}

static void dhcp_pump(void)
{
    if (!dhcp_auto || !timer_alive) return;
    u32 now = ticks;
    if (net_dhcp_ok) {
        u32 delta = now - dhcp_tick;
        dhcp_tick = now;
        u32 seconds = delta / 100;
        dhcp_fraction += delta % 100;
        seconds += dhcp_fraction / 100;
        dhcp_fraction %= 100;
        if (0xffffffff - dhcp_age < seconds) dhcp_age = 0xffffffff;
        else dhcp_age += seconds;
        int phase = dh_lease_phase(dhcp_age, dhcp_cfg.lease, dhcp_cfg.t1, dhcp_cfg.t2);
        if (phase == DH_OFF) {
            dhcp_clear_address();
            dhcp_begin();
        } else if (phase != dhcp_state) {
            if (dhcp_state == DH_BOUND) {
                dhcp_xid = api->rand() ^ ticks ^ ++dhcp_serial;
                dhcp_started = now;
            }
            dhcp_state = phase;
            dhcp_attempts = 0;
            dhcp_due = now;
        }
    }
    if (dhcp_state == DH_BOUND || (i32)(now - dhcp_due) < 0) return;
    if (dhcp_state == DH_OFF) dhcp_begin();
    if ((dhcp_state == DH_SELECT || dhcp_state == DH_REQUEST) && dhcp_attempts >= 4) {
        dhcp_state = DH_OFF;
        dhcp_due = now + 3000;
        return;
    }
    if (!dhcp_send()) { dhcp_due = now + 100; return; }
    u32 delay = 400u << (dhcp_attempts < 4 ? dhcp_attempts : 4);
    if (dhcp_attempts < 4) dhcp_attempts++;
    dhcp_due = now + delay + api->rand() % 100;
}

static void handle_frame(u8 *fr, u16 len)
{
    if(pm_active){pm_receive(fr,len);return;}
    capture_frame(fr,len);
    diag_rx++;
    if (len < 14) return;
    u16 type = ((u16)fr[12] << 8) | fr[13];

    if (type == 0x0806 && len >= 14 + sizeof(Arp)) {
        Arp *a = (Arp *)(fr + 14);
        if (a->htype != htons16(1) || a->ptype != htons16(0x0800) ||
            a->hlen != 6 || a->plen != 4 || (a->sha[0] & 1)) return;
        if (net_ip && a->op == htons16(1) && a->tpa == net_ip)
            arp_send(2, a->sha, a->spa);
        else if (a->op == htons16(2) && a->tpa == net_ip && nc_unicast(a->spa))
            arp_store(a->spa, a->sha);
        return;
    }

    if (type == 0x0800 && len >= 14 + 20) {
        Ip *ip = (Ip *)(fr + 14);
        if ((ip->vihl >> 4) != 4) return;
        u16 ihl = (ip->vihl & 0xF) * 4;
        u16 tl = htons16(ip->totlen);
        if (tl > len - 14 || ihl < 20 || tl < ihl ||
            (htons16(ip->frag) & 0x3fff) || csum(ip, ihl)) return;

        if (ip->proto == 17 && tl >= ihl + 8) {
            u8 *udp = (u8 *)ip + ihl;
            u16 dpt = (u16)(((u16)udp[2] << 8) | udp[3]);
            u16 ul  = (u16)(((u16)udp[4] << 8) | udp[5]);
            if (ul < 8 || ul > tl - ihl) return;
            if ((udp[6] || udp[7]) && nw_l4_csum(ip->src, ip->dst, 17, udp, ul)) return;
            if(dpt==KU_PUSH_PORT&&ip->dst==net_ip&&
               (((u16)udp[0]<<8)|udp[1])==KU_PUSH_PORT)
                push_receive(ip->src,fr+6,udp+8,ul-8);
            else if(dpt==DN_PORT&&ip->dst==net_ip&&
                    (((u16)udp[0]<<8)|udp[1])==DN_PORT)
                dn_receive(ip->src,fr+6,udp+8,ul-8);
            else if (dpt == 68 && udp[0] == 0 && udp[1] == 67)
                dhcp_recv(udp + 8, ul - 8);
            else if (dpt == DNS_SPORT && ip->dst == net_ip && ip->src == dns_expected && udp[0] == 0 && udp[1] == 53) {
                u32 a = nw_dns_parse(udp + 8, ul - 8, dns_id_cur);
                if (a) dns_answer = a;
            }
            else if (dpt == SNTP_SPORT && ip->dst == net_ip) {
                const u8 *p = udp + 8;
                if (ul - 8 >= 48 && ((p[0] & 7) == 4 || (p[0] & 7) == 5))
                    sntp_answer = ((u32)p[40] << 24) | ((u32)p[41] << 16) |
                                  ((u32)p[42] << 8)  |  (u32)p[43];
            }
            return;
        }

        if (ip->dst != net_ip) return;

        if (ip->proto == 6 && tl >= ihl + 20) {
            u8 *th = (u8 *)ip + ihl;
            u16 sp = (u16)(((u16)th[0] << 8) | th[1]);
            u16 dp = (u16)(((u16)th[2] << 8) | th[3]);
            if (nw_tcp_csum(ip->src, ip->dst, th, tl - ihl)) return;
            if (nl_input(ip->src,fr+6,th,tl-ihl)) return;
            if (ip->src != tcp_rip || sp != tcp_rport || dp != tcp_lport)
                return;
            int thl = (th[12] >> 4) * 4;
            int plen = (int)tl - ihl - thl;
            if (thl < 20 || plen < 0) return;
            u32 seq = ((u32)th[4] << 24) | ((u32)th[5] << 16) |
                      ((u32)th[6] << 8) | th[7];
            u32 ack = ((u32)th[8] << 24) | ((u32)th[9] << 16) |
                      ((u32)th[10] << 8) | th[11];
            int off, dl;
            int a = tcp_input(&tcb, th[13] & 0x1F, seq, ack, plen, &off, &dl);
            if (a) tcp_act(a, th + thl, off, dl);
            return;
        }

        if (ip->proto != 1 || tl < ihl + 8) return;

        Icmp *ic = (Icmp *)((u8 *)ip + ihl);
        u16 ilen = tl - ihl;
        if (csum(ic, ilen) || ic->code) return;
        if (ic->type == 8) {
            ic->type = 0;
            ic->csum = 0;
            ic->csum = csum(ic, ilen);
            u8 smac[6];
            memcpy(smac, fr + 6, 6);
            arp_store(ip->src, smac);
            ip_send(ip->src, smac, 1, (u8 *)ic, ilen);
        } else if (ic->type == 0 && ip->src == ping_target && ic->id == htons16(0x464c) && ic->seq == htons16(ping_seq)) {
            ping_got = 1;
        }
    }
}

static int net_dhcp_locked(u32 timeout)
{
    if (!net_up() || !timer_alive || dhcp_busy) return 0;
    dhcp_busy = 1;
    dhcp_auto = 1;
    net_cancel = 0;
    dhcp_clear_address();
    dhcp_begin();
    u32 start = ticks;
    do {
        net_wait();
    } while (!net_dhcp_ok && !net_cancel && (u32)(ticks - start) < timeout);
    if (net_cancel) {
        dhcp_state = DH_OFF;
        dhcp_due = ticks + 3000;
    }
    dhcp_busy = 0;
    return net_dhcp_ok;
}
int net_dhcp(u32 timeout){api->network_lock();int result=net_dhcp_locked(timeout);api->network_unlock();return result;}


static void tcp_pump(void);

static void ne2k_pull(void)
{
    for (int guard = 0; guard < 64; guard++) {
        outb(io + CR, 0x62);
        u8 curr = inb(io + 7);
        outb(io + CR, 0x22);
        u8 bnry = inb(io + BNRY);
        u8 next = bnry + 1;
        if (next >= PG_RSTOP) next = PG_RSTART;
        if (next == curr) break;

        u8 hdr[4];
        rd_remote((u16)next << 8, hdr, 4);
        u8 nextpg = hdr[1];
        u16 plen = hdr[2] | ((u16)hdr[3] << 8);
        if (nextpg < PG_RSTART || nextpg >= PG_RSTOP || plen < 18 || plen > 1600) {
            outb(io + BNRY, PG_RSTART);
            outb(io + CR, 0x62);
            outb(io + 7, PG_RSTART + 1);
            outb(io + CR, 0x22);
            break;
        }

        static u8 pkt[1600];
        u16 dlen = plen - 4;
        u8 *buf = rxq.buf ? (dlen <= RXQ_FRAME ? rxq_slot(&rxq) : 0) : pkt;
        u16 addr = ((u16)next << 8) + 4;
        u16 tail = ((u16)PG_RSTOP << 8) - addr;
        if (buf && dlen <= tail) {
            rd_remote(addr, buf, dlen);
        } else if (buf) {
            rd_remote(addr, buf, tail);
            rd_remote((u16)PG_RSTART << 8, buf + tail, dlen - tail);
        }
        if (buf == pkt) handle_frame(pkt, dlen);
        else if (buf) rxq_commit(&rxq, dlen);

        u8 nb = (nextpg == PG_RSTART) ? PG_RSTOP - 1 : nextpg - 1;
        outb(io + BNRY, nb);
        outb(io + ISR, 0x01);
    }
}

static void nic_pull(void)
{
    if (nic_kind == NIC_PCNET) pc_poll();
    else if (nic_kind == NIC_RTL8139) rtl_poll_hw();
    else if (nic_kind == NIC_TULIP) tul_poll_hw();
    else if (nic_kind == NIC_NE2K) ne2k_pull();
}

static void nic_poll(void)
{
    if (nic_kind == NIC_NONE) return;
    u32 f = net_irq_save();
    nic_pull();
    u16 len; u8 *frame;
    for (int n = 0; n < RXQ_N && (frame = rxq_peek(&rxq, &len)); n++) {
        handle_frame(frame, len);
        rxq_pop(&rxq);
    }
    net_irq_restore(f);
}

static void rx_irq_mask(void)
{
    if (nic_kind == NIC_TULIP) outl(T_CSR(7), 0);
    else if (nic_kind == NIC_RTL8139) outw(rio + 0x3C, 0);
    else if (nic_kind == NIC_PCNET) { pc_ien = 0; pc_set(3, 0x5f00); pc_set(0, 0); }
    else if (nic_kind == NIC_NE2K) outb(io + IMR, 0);
}

static void rx_irq_stop(void)
{
    if (!rx_irq_on) return;
    rx_irq_mask();
    api->irq_unregister(nic_irq);
    rx_irq_on = 0; rx_irq_stopped = 1;
}

static int nic_isr_pull(void)
{
    if (nic_kind == NIC_TULIP) {
        u32 st = inl(T_CSR(5)) & 0x0001FFFFu;
        if (!st) return 0;
        outl(T_CSR(5), st);
        tul_poll_hw();
        if (st & 0x80) outl(T_CSR(2), 1);
        return 1;
    }
    if (nic_kind == NIC_RTL8139) {
        u16 st = inw(rio + 0x3E);
        if (!st || st == 0xFFFF) return 0;
        outw(rio + 0x3E, st);
        rtl_poll_hw();
        return 1;
    }
    if (nic_kind == NIC_PCNET) {
        if (!(pc_csr(0) & 0x0080)) return 0;
        pc_poll();
        return 1;
    }
    if (nic_kind == NIC_NE2K) {
        u8 st = inb(io + ISR) & 0x11;
        if (!st) return 0;
        outb(io + ISR, st);
        ne2k_pull();
        return 1;
    }
    return 0;
}

static void nic_isr(void)
{
    rx_irq_calls++;
    if (rx_irq_tick != ticks) { rx_irq_tick = ticks; rx_irq_burst = 0; }
    if (++rx_irq_burst > 2000) { rx_irq_stop(); return; }
    if (nic_isr_pull()) { rx_irq_idle = 0; return; }
    rx_irq_empty++;
    if (++rx_irq_idle > 5000) rx_irq_stop();
}

static void rx_irq_start(void)
{
    int irq = nic_irq;
    if (rx_irq_on || !rxq.buf || irq < 3 || irq > 15 || irq == 6 || irq == 8 ||
        irq == 12 || irq == 13 || (nic_kind == NIC_NE2K && !ne_is_pci)) return;
    u32 f = net_irq_save();
    if (api->irq_register(irq, nic_isr) == 0) {
        rx_irq_on = 1;
        if (nic_kind == NIC_TULIP) outl(T_CSR(7), 0x000180C0u);
        else if (nic_kind == NIC_RTL8139) outw(rio + 0x3C, 0x0051);
        else if (nic_kind == NIC_PCNET) { pc_ien = 0x40; pc_set(3, 0x5b00); pc_set(0, 0x40); }
        else outb(io + IMR, 0x11);
    }
    net_irq_restore(f);
}

static u32 rx_slots(void)
{
    u32 slots = nic_kind == NIC_TULIP ? (u32)tul_nrx : nic_kind == NIC_PCNET ? (u32)pc_nrx :
                nic_kind == NIC_RTL8139 ? rtl_ring / 1600 : (u32)(PG_RSTOP - PG_RSTART) * 256 / 1600;
    return rxq.buf && slots > RXQ_N ? RXQ_N : slots;
}

static void net_poll_inner(void)
{
    if(nic_kind==NIC_NONE)return;
    dhcp_pump();
    nic_poll();
    dhcp_pump();
    tcp_pump();
    nl_pump();
}

void net_poll(void)
{
    u32 f=net_irq_save();
    net_poll_inner();
    dn_poll();
    faultnet_poll();
    push_poll();
    pm_cache();
    net_irq_restore(f);
}

static int arp_resolve(u32 ip, u8 *mac, u32 timeout)
{
    if (!ip) return 0;
    if (arp_lookup(ip, mac)) return 1;
    static const u8 zero[6] = {0,0,0,0,0,0};
    arp_send(1, zero, ip);
    u32 t0 = ticks, sent=t0;
    while (!arp_lookup(ip, mac)) {
        if (net_cancel || (u32)(ticks - t0) > timeout) return 0;
        if((u32)(ticks-sent)>=100){arp_send(1,zero,ip);sent=ticks;}
        net_wait();
    }
    return 1;
}

static int net_ping_locked(u32 dst, u32 timeout)
{
    if (!net_up()) return -2;
    if (!timer_alive) return -1;
    net_cancel = 0;
    u32 hop = ((dst & net_mask) == (net_ip & net_mask)) ? dst : net_gw;
    u8 mac[6];
    if (!net_ip || !hop || !nc_unicast(dst)) return -2;
    if (!arp_resolve(hop, mac, timeout)) return net_cancel ? -1 : -2;

    ping_seq++;
    ping_target = dst; ping_got = 0;
    u8 msg[40];
    Icmp *ic = (Icmp *)msg;
    ic->type = 8; ic->code = 0;
    ic->id = htons16(0x464C);
    ic->seq = htons16(ping_seq);
    ic->csum = 0;
    for (int i = 8; i < 40; i++) msg[i] = 'a' + (i & 15);
    ic->csum = csum(msg, 40);
    ip_send(dst, mac, 1, msg, 40);

    u32 t0 = ticks;
    while (!ping_got) {
        if (net_cancel || (u32)(ticks - t0) > timeout) return -1;
        net_wait();
    }
    return (int)(ticks - t0) * 10;
}
int net_ping(u32 dst,u32 timeout){api->network_lock();int result=net_ping_locked(dst,timeout);api->network_unlock();return result;}


static u32 net_link_bits(void)
{
    PhyState p;
    if (nic_kind == NIC_TULIP) {
        tul_link(&p);
    } else if (nic_kind == NIC_RTL8139) {

        u8 msr = inb(rio + 0x58);
        p.present = 1;
        p.up = (msr & 0x04) ? 0 : 1;
        p.autoneg_done = 1;
        p.mbps = (u16)((msr & 0x08) ? 10 : 100);
        p.full = 0;
        if (!p.up) p.mbps = 0;
    } else {
        return 0;
    }
    u32 v = 0;
    if (p.present) v |= NET_LINK_PHY;
    if (p.up)      v |= NET_LINK_UP;
    if (p.full)    v |= NET_LINK_FULL;
    return v | ((u32)p.mbps << 8);
}

static u32 net_getv(int what)
{
    switch (what) {
    case NET_IP:      return net_ip;
    case NET_MASK:    return net_mask;
    case NET_GW:      return net_gw;
    case NET_DHCP_OK: return net_dhcp_ok;
    case NET_DNS:     return net_dns_srv;
    case NET_LINK:    return net_link_bits();
    case NET_ADAPTER: return nic_kind == NIC_NE2K && !ne_is_pci ? 5 : nic_kind;
    case NET_IO:      return nic_kind == NIC_NE2K ? io : nic_kind == NIC_RTL8139 ? rio : nic_kind == NIC_TULIP ? tul_io : nic_kind == NIC_PCNET ? pc_io : 0;
    case NET_STATE:   return dhcp_auto ? dhcp_state : 0;
    case NET_RX:      return diag_rx;
    case NET_TX:      return diag_tx;
    case NET_LEASE_LEFT: return net_dhcp_ok && dhcp_cfg.lease > dhcp_age ? dhcp_cfg.lease - dhcp_age : 0;
    case NET_DNS2:    return dns_second;
    case NET_MTU:     return prefs.mtu;
    }
    return 0;
}
static void net_setv(int what, u32 v)
{
    if (what == NET_IP || what == NET_MASK || what == NET_GW)
        for (int i = 0; i < 4; i++) arpc[i].valid = 0;
    switch (what) {
    case NET_IP:      net_ip = v; break;
    case NET_MASK:    net_mask = v; break;
    case NET_GW:      net_gw = v; break;
    case NET_DHCP_OK:
        net_dhcp_ok = (u8)v;
        if (!v) { dhcp_auto = 0; dhcp_state = DH_OFF; }
        break;
    case NET_DNS:     net_dns_srv = v; break;
    case NET_DNS2:    dns_second = v; break;
    case NET_CONFIG:
        np_load(CFG, &prefs);
        if (CFG->net_mode || prefs.dns_manual) { net_dns_srv = prefs.dns; dns_second = prefs.dns2; }
        else if (net_dhcp_ok) { net_dns_srv = dhcp_cfg.dns; dns_second = dhcp_cfg.dns2; }
        break;
    case NET_RENEW:
        if (net_up()) { dhcp_auto = 1; dhcp_clear_address(); dhcp_begin(); }
        break;
    }
}
static const u8 *net_macf(void) { return net_mac; }

static const NetOps net_ops = {
    .poll = net_poll,
    .up   = net_up,
    .dhcp = net_dhcp,
    .ping = net_ping,
    .get  = net_getv,
    .set  = net_setv,
    .mac  = net_macf,
    .v11  = NET_ABI_V11,
    .dns  = net_dns_resolve,
    .http_get = net_http_get_impl,
    .v12  = NET_ABI_V12,
    .sntp = net_sntp_impl,
};

static void pr(const char *s) { api->shell_print(s); }

static void cmd_netdiag(const char *args)
{
    (void)args;
    char b[100];

    if (nic_kind == NIC_NONE) {
        pr("netdiag: no NIC claimed.\n");
        pr("  Nothing matched the NE2000 / RTL8139 / Tulip / PCnet probes.\n");
        pr("  Run 'lspci' - if a network card is listed there, its PCI id\n");
        pr("  is not in this driver yet.\n");
        return;
    }

    const char *kn = nic_kind == NIC_NE2K    ? (ne_is_pci ? "NE2000 (PCI)" : "NE2000 (ISA)")
                   : nic_kind == NIC_RTL8139 ? "RTL8139"
                   : nic_kind == NIC_PCNET ? "AMD PCnet/PCI"
                   : tul_admtek              ? "ADMtek Comet/Centaur (NC100)"
                                             : "DEC 21x4x tulip";
    u16 base = nic_kind == NIC_NE2K ? io : nic_kind == NIC_RTL8139 ? rio : nic_kind == NIC_PCNET ? pc_io : tul_io;
    kfmt(b, sizeof b, "netdiag: %s at I/O %04xh\n", kn, base);
    pr(b);

    kfmt(b, sizeof b, "  mac    %02x:%02x:%02x:%02x:%02x:%02x%s\n",
              net_mac[0], net_mac[1], net_mac[2], net_mac[3], net_mac[4],
              net_mac[5],
              mac_is_fallback ? "  <- FALLBACK, the card gave us nothing" : "");
    pr(b);

    u32 lk = net_link_bits();
    if (!(lk & NET_LINK_PHY)) {
        pr("  link   no PHY to ask (this card cannot report link)\n");
    } else if (!(lk & NET_LINK_UP)) {
        pr("  link   DOWN - no signal from the other end.\n");
        pr("         Check the cable, the switch port, and its link LED.\n");
    } else {
        kfmt(b, sizeof b, "  link   UP, %u Mbit/s %s duplex\n",
                  (unsigned)NET_LINK_MBPS(lk),
                  (lk & NET_LINK_FULL) ? "full" : "half");
        pr(b);
    }

    if (nic_kind == NIC_TULIP) {
        kfmt(b, sizeof b, "  csr0   %08x   csr5 %08x   csr6 %08x\n",
                  inl(T_CSR(0)), inl(T_CSR(5)), inl(T_CSR(6)));
        pr(b);
        if (tul_admtek) {
            kfmt(b, sizeof b, "  mii    bmcr %04x bmsr %04x anar %04x anlpar %04x; opr %08x\n",
                      tul_mii(0), tul_mii(1), tul_mii(4), tul_mii(5), inl(tul_io + 0xFC));
            pr(b);
        }
        kfmt(b, sizeof b, "  rings  rx slot %d/%d, tx slot %d\n",
                  tul_rxi, tul_nrx, tul_txi);
        pr(b);
    }

    kfmt(b, sizeof b, "  frames rx %u  tx %u  arp entries %u\n",
              (unsigned)diag_rx, (unsigned)diag_tx, (unsigned)arp_count());
    pr(b);
    if (rx_irq_on) kfmt(b, sizeof b, "  rx     interrupt IRQ %d, %u calls (%u empty); queue peak %u/%d, %u dropped\n",
                        nic_irq, rx_irq_calls, rx_irq_empty, rxq.peak, RXQ_N, rxq.drops);
    else kfmt(b, sizeof b, "  rx     polled%s; %u slots; queue peak %u/%d, %u dropped\n",
              rx_irq_stopped ? " (interrupts stopped: storm)" : "", rx_slots(), rxq.peak, RXQ_N, rxq.drops);
    pr(b);
    kfmt(b, sizeof b, "  ip     %d.%d.%d.%d  (%s)\n",
              net_ip & 0xFF, (net_ip >> 8) & 0xFF, (net_ip >> 16) & 0xFF,
              net_ip >> 24, net_dhcp_ok ? "DHCP" : "static");
    pr(b);
    if (!net_ip)
        pr("  no address: run 'set net dhcp', or 'ifconfig a.b.c.d' to set one.\n");
    static const char *const states[] = {
        "waiting", "discovering", "requesting", "bound", "renewing", "rebinding"
    };
    kfmt(b, sizeof b, "  DHCP   %s; offers %u ACKs %u NAKs %u ignored %u\n",
         dhcp_auto ? states[dhcp_state] : "disabled", dhcp_offers,
         dhcp_acks, dhcp_naks, dhcp_rejected);
    pr(b);
    if (net_dhcp_ok) {
        kfmt(b, sizeof b, "  lease  %u seconds; age %u; renew %u; rebind %u\n",
             dhcp_cfg.lease, dhcp_age, dhcp_cfg.t1, dhcp_cfg.t2);
        pr(b);
    }
    if ((lk & NET_LINK_PHY) && !(lk & NET_LINK_UP))
        pr("  Nothing can work until link comes up. Fix that first.\n");
    else if (!diag_rx)
        pr("  Zero frames received. If link is up, suspect the RX ring/DMA.\n");
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "Networking"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    net_init();
    api->register_net(&net_ops);
    api->register_service("net.text",&text_ops);
    api->register_service("net.listen",&listen_ops);
    api->register_service("net.http",&http_ops);
    api->register_service("net.http.form",&http_form_ops);
    api->register_service("net.http.diag",&http_diag_ops);
    api->register_service("net.update",&push_ops);
    api->register_service("net.debug",&dn_ops);
    u32 automatic=0;api->config_get("debugnet.auto",&automatic);dn_set_auto(automatic==1);
    api->register_cmd("debugnet","debugnet auto | <PC-IP> [port] | status | test | off - send crash reports",cmd_debugnet);
    api->register_cmd("netdiag", "netdiag - report NIC, link and traffic state",
                      cmd_netdiag);
    return 0;
}
