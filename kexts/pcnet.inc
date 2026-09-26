/* Sends and receives packets with an AMD PCnet adapter. */
static u16 pc_io, pc_chip;
typedef struct { u32 addr; u16 count; volatile u16 status; u32 misc, reserved; } PcDesc;
static PcDesc pc_rx_static[4] __attribute__((aligned(16)));
static PcDesc *pc_rx = pc_rx_static;
static u8 *pc_rxb;
static int pc_nrx = 4, pc_log = 2;
static u16 pc_ien;
static PcDesc pc_txring[2] __attribute__((aligned(16)));
static struct __attribute__((packed,aligned(16))) {
    u16 mode; u8 rlen, tlen; u8 mac[6]; u16 reserved;
    u32 filter[2], rx, tx;
} pc_block;
static int pc_ri, pc_ti;
static u16 pc_csr(int index) { outw(pc_io + 0x12, index); return inw(pc_io + 0x10); }
static void pc_set(int index, u16 value) { outw(pc_io + 0x12, index); outw(pc_io + 0x10, value); }
static u16 pc_bcr(int index) { outw(pc_io + 0x12, index); return inw(pc_io + 0x16); }
static void pc_bset(int index, u16 value) { outw(pc_io + 0x12, index); outw(pc_io + 0x16, value); }
static int pc_init(void)
{
    (void)inw(pc_io + 0x14);
    if (pc_csr(0) != 4) return 0;
    outw(pc_io + 0x12, 88);
    if (inw(pc_io + 0x12) != 88) return 0;
    pc_chip = ((u32)pc_csr(88) | ((u32)pc_csr(89) << 16)) >> 12;
    int any = 0;
    for (int i = 0; i < 6; i++) { net_mac[i] = inb(pc_io + i); any |= net_mac[i]; }
    if (!any || (net_mac[0] & 1)) return 0;
    pc_set(0, 4);
    pc_bset(20, 2);
    pc_bset(2, pc_bcr(2) | 2);
    if (pc_chip != 0x2420 && pc_chip != 0x2621)
        pc_bset(32, (pc_bcr(32) & ~0x98u) | 0x20);
    pc_bset(9, pc_bcr(9) & ~3u);
    memset(&pc_block, 0, sizeof pc_block);
    if (pc_rx == pc_rx_static) {
        u8 *big = nic_dma_alloc(32 * (sizeof(PcDesc) + 1536), 16);
        if (big) { pc_rx = (PcDesc *)big; pc_rxb = big + 32 * sizeof(PcDesc); pc_nrx = 32; pc_log = 5; }
        else pc_rxb = rtl_rx_static;
    }
    memset(pc_rx, 0, pc_nrx * sizeof(PcDesc));
    memset(pc_txring, 0, sizeof pc_txring);
    pc_block.rlen = (u8)(pc_log << 4); pc_block.tlen = 1 << 4;
    memcpy(pc_block.mac, net_mac, 6);
    pc_block.rx = (u32)pc_rx; pc_block.tx = (u32)pc_txring;
    for (int i = 0; i < pc_nrx; i++) {
        pc_rx[i].addr = (u32)(pc_rxb + i * 1536);
        pc_rx[i].count = (u16)-1536;
        pc_rx[i].status = 0x8000;
    }
    for (int i = 0; i < 2; i++) pc_txring[i].addr = (u32)rtl_txb[i];
    pc_ri = pc_ti = 0;
    pc_set(1, (u32)&pc_block); pc_set(2, (u32)&pc_block >> 16);
    pc_set(3, 0x5f00);
    __asm__ volatile("" ::: "memory");
    pc_set(0, 1);
    int n = 200000;
    while (!(pc_csr(0) & 0x0100) && --n) ;
    if (!n) { pc_set(0, 4); return 0; }
    pc_set(0, 0x0102);
    return 1;
}
static void pc_tx(const u8 *frame, u16 len)
{
    PcDesc *d = &pc_txring[pc_ti];
    if (len > 1514 || (d->status & 0x8000)) return;
    memcpy(rtl_txb[pc_ti], frame, len);
    while (len < 60) rtl_txb[pc_ti][len++] = 0;
    d->count = (u16)-len; d->misc = d->reserved = 0;
    __asm__ volatile("" ::: "memory");
    d->status = 0x8300;
    pc_ti = (pc_ti + 1) & 1;
    pc_set(0, 8 | pc_ien);
}
static void pc_poll(void)
{
    for (int n = 0; n < pc_nrx; n++) {
        PcDesc *d = &pc_rx[pc_ri];
        u16 status = d->status;
        if (status & 0x8000) break;
        __asm__ volatile("" ::: "memory");
        int len = (d->misc & 0xfff) - 4;
        if ((status & 0x4300) == 0x0300 && len >= 14 && len <= 1514)
            rx_push(pc_rxb + pc_ri * 1536, (u16)len);
        d->misc = d->reserved = 0; d->count = (u16)-1536;
        __asm__ volatile("" ::: "memory");
        d->status = 0x8000;
        pc_ri = (pc_ri + 1) % pc_nrx;
    }
    pc_set(0, (pc_csr(0) & 0x7f00) | pc_ien);
}
