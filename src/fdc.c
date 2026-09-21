/* Reads and writes floppy sectors through DMA channel 2. */
#include "os.h"
#include "ioguard.inc"
#include "floppy.h"

#define DOR  0x3F2
#define MSR  0x3F4
#define FIFO 0x3F5
#define CCR  0x3F7

#define MOTOR_OFF_TICKS 300

volatile u8 fdc_irq_fl;

static u8  motor_on_fl;
static u32 last_use;
static int cur_cyl = -1;

static void io_delay(void) { for (int i = 0; i < 4; i++) inb(0x80); }

#define FIFO_PUMP_EVERY 0xFFF

static int wait_write(void)
{
    for (int i = 0; i < 200000; i++) {
        if ((inb(MSR) & 0xC0) == 0x80) return 1;
        if ((i & FIFO_PUMP_EVERY) == FIFO_PUMP_EVERY) gui_pump();
    }
    return 0;
}

static int wait_read(void)
{
    for (int i = 0; i < 200000; i++) {
        if ((inb(MSR) & 0xC0) == 0xC0) return 1;
        if ((i & FIFO_PUMP_EVERY) == FIFO_PUMP_EVERY) gui_pump();
    }
    return 0;
}

static int fdc_out(u8 b)
{
    if (!wait_write()) return 0;
    outb(FIFO, b);
    return 1;
}

static int fdc_in(u8 *b)
{
    if (!wait_read()) return 0;
    *b = inb(FIFO);
    return 1;
}

static int wait_irq(u32 timeout)
{
    u32 t0 = ticks, guard = 0;
    u32 lim = timer_alive ? 200000000u : 2000000u;
    while (!fdc_irq_fl) {
        if ((u32)(ticks - t0) > timeout) return 0;
        if (++guard > lim) return 0;
        gui_pump();

    }
    fdc_irq_fl = 0;
    return 1;
}

static void sleep_ticks(u32 n)
{
    u32 t0 = ticks, guard = 0;
    u32 lim = timer_alive ? 200000000u : 2000000u;
    while ((u32)(ticks - t0) < n && ++guard < lim) gui_pump();
}

static void sense_int(u8 *st0, u8 *cyl)
{
    fdc_out(0x08);
    fdc_in(st0);
    fdc_in(cyl);
}

static void motor(int on)
{
    if (on) {
        last_use = ticks;
        if (motor_on_fl) return;
        outb(DOR, 0x1C);
        motor_on_fl = 1;
        sleep_ticks(30);
    } else {
        outb(DOR, 0x0C);
        motor_on_fl = 0;
    }
}

void fdc_tick(void)
{
    if (!motor_on_fl) return;

    if (!timer_alive || (u32)(ticks - last_use) > MOTOR_OFF_TICKS)
        motor(0);
}

static int recalibrate(void)
{
    u8 st0, cyl;
    motor(1);
    fdc_irq_fl = 0;
    fdc_out(0x07);
    fdc_out(0x00);
    if (!wait_irq(200)) return 0;
    sense_int(&st0, &cyl);
    cur_cyl = 0;
    return (st0 & 0xC0) == 0;
}

static int seek(int cyl, int head)
{
    if (cur_cyl == cyl) return 1;
    u8 st0, pcn;
    fdc_irq_fl = 0;
    fdc_out(0x0F);
    fdc_out(head << 2);
    fdc_out(cyl);
    if (!wait_irq(200)) return 0;
    sense_int(&st0, &pcn);
    if (pcn != cyl) return 0;
    cur_cyl = cyl;
    sleep_ticks(2);
    return 1;
}

static void dma_setup(int to_mem, u32 len)
{
    u32 addr = (u32)DMABUF;
    outb(0x0A, 0x06);
    outb(0x0C, 0xFF);
    outb(0x04, addr & 0xFF);
    outb(0x04, (addr >> 8) & 0xFF);
    outb(0x81, (addr >> 16) & 0xFF);
    outb(0x0C, 0xFF);
    outb(0x05, (len - 1) & 0xFF);
    outb(0x05, ((len - 1) >> 8) & 0xFF);
    outb(0x0B, to_mem ? 0x46 : 0x4A);
    outb(0x0A, 0x02);
}

int fdc_ok;

void fdc_init(void)
{
    u8 st0, cyl;
    fdc_irq_fl = 0;
    outb(DOR, 0x00);
    io_delay();
    outb(DOR, 0x0C);
    bmark('1');
    fdc_ok = wait_irq(100);
    bmark(fdc_ok ? '2' : 'x');
    for (int i = 0; i < 4; i++) sense_int(&st0, &cyl);
    bmark('3');
    outb(CCR, 0x00);
    fdc_out(0x03);
    fdc_out(0xDF);
    fdc_out(0x02);
    bmark('4');
    cur_cyl = -1;
    motor_on_fl = 0;
}

static u32 fh_ops, fh_retried, fh_failed;
static u32 fh_last_ms, fh_worst_ms, fh_last_tries, fh_last_lba;
static u8  fh_st[3];

void fdc_result_get(FdcResult *r)
{
    r->lba=fh_last_lba;r->ms=fh_last_ms;r->tries=fh_last_tries;
    memcpy(r->st,fh_st,sizeof fh_st);
}

void fdc_result_restore(const FdcResult *r)
{
    fh_last_lba=r->lba;fh_last_ms=r->ms;fh_last_tries=r->tries;
    memcpy(fh_st,r->st,sizeof fh_st);
}

u32 fdc_stat(int what)
{
    switch (what) {
    case DS_OPS:        return fh_ops;
    case DS_RETRIED:    return fh_retried;
    case DS_FAILED:     return fh_failed;
    case DS_LAST_MS:    return fh_last_ms;
    case DS_WORST_MS:   return fh_worst_ms;
    case DS_LAST_TRIES: return fh_last_tries;
    case DS_LAST_LBA:   return fh_last_lba;
    case DS_ST0:        return fh_st[0];
    case DS_ST1:        return fh_st[1];
    case DS_ST2:        return fh_st[2];
    }
    return 0;
}

static void fh_record(u32 lba, int tries, u32 t0, int rc)
{
    fh_ops++;
    fh_last_lba = lba;
    fh_last_tries = (u32)tries;
    fh_last_ms = (u32)(ticks - t0) * 10;
    if (fh_last_ms > fh_worst_ms) fh_worst_ms = fh_last_ms;
    if (tries > 1) fh_retried++;
    if (rc != 0) fh_failed++;
}

static int fdc_rw(u32 lba, u8 *buf, int write, u32 count)
{
    if (lba >= 2880 || !count || count > FLOPPY_TRACK_SECTORS-lba%FLOPPY_TRACK_SECTORS) return -1;
    int c = lba / 36, h = (lba / 18) % 2, s = lba % 18 + 1;
    u8 res[7];
    fh_st[0] = fh_st[1] = fh_st[2] = 0;
    u32 t0 = ticks;
    int tries = 0;

    motor(1);
    for (int attempt = 0; attempt < 4; attempt++) {
        tries = attempt + 1;
        if (attempt) { recalibrate(); }
        if (!seek(c, h)) continue;
        if (write) memcpy(DMABUF, buf, count*512);
        dma_setup(!write, count*512);
        fdc_irq_fl = 0;
        fdc_out(write ? 0x45 : 0x46);
        fdc_out((h << 2) | 0);
        fdc_out(c);
        fdc_out(h);
        fdc_out(s);
        fdc_out(2);
        fdc_out(18);
        fdc_out(0x1B);
        fdc_out(0xFF);
        if (!wait_irq(200)) { fdc_init(); continue; }
        int ok = 1;
        for (int i = 0; i < 7; i++)
            if (!fdc_in(&res[i])) { ok = 0; break; }
        if (!ok) { fdc_init(); continue; }
        fh_st[0] = res[0]; fh_st[1] = res[1]; fh_st[2] = res[2];
        if ((res[0] & 0xC0) == 0) {
            if (!write) memcpy(buf, DMABUF, count*512);
            last_use = ticks;
            fh_record(lba, tries, t0, 0);
            app_note_io();
            return 0;
        }
    }
    fh_record(lba, tries, t0, -1);
    return -1;
}

static Mutex fdc_mutex=MUTEX_INIT;

static int fdc_guarded(u32 lba, u8 *buf, int write, u32 count)
{
    if(!buf||lba>=2880||!count||count>2880-lba)return -1;
    if(fdc_mutex.held&&fdc_mutex.owner==thr_self)return -1;
    mtx_lock(&fdc_mutex);
    int rc=0;
    while(count){
        u32 n=FLOPPY_TRACK_SECTORS-lba%FLOPPY_TRACK_SECTORS;if(n>count)n=count;
        if(write)for(u32 i=0;i<n;i++)fs_cache_invalidate(lba+i);
        rc=fdc_rw(lba,buf,write,n);debug_disk(0,write,lba,n,rc);if(rc)break;
        lba+=n;buf+=n*512;count-=n;
    }
    mtx_unlock(&fdc_mutex);
    return rc;
}

int fdc_read(u32 lba, u8 *buf)        { return fdc_guarded(lba, buf, 0, 1); }
int fdc_write(u32 lba, const u8 *buf) { return fdc_guarded(lba, (u8 *)buf, 1, 1); }
int fdc_read_many(u32 lba,u8 *buf,u32 count){return fdc_guarded(lba,buf,0,count);}
int fdc_write_many(u32 lba,const u8 *buf,u32 count){return fdc_guarded(lba,(u8 *)buf,1,count);}
