/* Starts the kernel and runs its event loop. */
#include "os.h"
#include "flipgate.inc"
#include "flightgate.inc"
#include "flightlog.inc"
#include "platform.inc"

#include "kbcore.inc"
#include "hangwatch.inc"
#include "axline.inc"

static KbSt kb = { 0, 0, 0, 0, 0, KB_NUMLOCK_DEFAULT, 0, 0 };

int kbd_mods(void)
{
    return ((kb.shift_l || kb.shift_r) ? 1 : 0) | (kb.ctrl ? 2 : 0) |
           (kb.caps ? 4 : 0) | ((kb.alt_l || kb.alt_r) ? 8 : 0);
}

static u8 keydown[64];

static int key_slot(int k)
{
    if (k >= 'A' && k <= 'Z') k += 'a' - 'A';
    return (k >= 0 && k < 512) ? k : -1;
}

static void key_state(int k, int down)
{
    int s = key_slot(k);
    if (s < 0) return;
    if (down) keydown[s >> 3] |= (u8)(1u << (s & 7));
    else      keydown[s >> 3] &= (u8)~(1u << (s & 7));
}

int key_is_down(int k)
{
    int s = key_slot(k);
    return s < 0 ? 0 : (keydown[s >> 3] >> (s & 7)) & 1;
}

void key_clear_held(void) { memset(keydown, 0, sizeof keydown); }

static void handle_sc(u8 sc)
{

    int track, down;
    int deliver = kb_feed(&kb, sc, &track, &down);
    if (track) key_state(track, down);
    if (deliver) gui_key(deliver);
}

u8 timer_alive;
char boot_errs[48];

static int bmx = 8, bmy = 8;
static void bscroll(void)
{
    while (bmy + 16 > SH - 8) {
        memmove(BACKBUF, BACKBUF + 16 * SW, (u32)SW * (SH - 16));
        memset(BACKBUF + (u32)SW * (SH - 16), C_DESK, 16 * SW);
        bmy -= 16;
    }
}
static void bprintc(const char *s, u8 col)
{
    for (; *s; s++) {
        if (*s == '\n') { bmx = 8; bmy += 16; bscroll(); continue; }
        draw_char(bmx, bmy, *s, col);
        bmx += 8;
    }
    flip();
}
static void bprint(const char *s) { bprintc(s, C_WHITE); }
void boot_print(const char *s) { bprintc(s, C_WHITE); }

static void bfail(const char *code)
{
    char b[24];
    kfmt(b, sizeof b, " FAIL (%s)\n", code);
    bprintc(b, C_RED);
    int n = strlen(boot_errs);
    kfmt(boot_errs + n, (int)sizeof boot_errs - n, " %s", code);
}
void boot_fail(const char *code) { bfail(code); }

static u8 checks_active;
void bmark(char c)
{
    if (!checks_active) return;
    char b[2] = { c, 0 };
    bprintc(b, C_SILVER);
}

static u16 pit_read(void)
{
    outb(0x43, 0x00);
    u16 lo = inb(0x40);
    return (u16)(lo | ((u16)inb(0x40) << 8));
}
static u32 pit_step(u16 *prev)
{
    u16 now = pit_read();
    u16 d = (u16)(*prev - now);
    *prev = now;
    return d > 11932 ? 0 : d;
}

static void hold_ms(u32 ms)
{
    u16 prev = pit_read();
    u32 acc = 0, it = 0, target = ms * 2386;
    while (acc < target && ++it <= 50000000u)
        acc += pit_step(&prev);
}

static const char *lp_name[10] = {
    "input", "gui_tick", "fdc", "net", "usb",
    "timers", "cpu_acct", "flight", "compose", "flip"
};
u32 lp_pmax[10];
u32 lp_worst;
u32 lp_cmp_n, lp_cmp_us;
u32 lp_flips, lp_gapmax;

void loop_prof_fmt(char *out, int cap)
{
    u32 mhz = cpu_mhz(); if (!mhz) mhz = 100;
    u32 cyc_ms = mhz * 1000;
    static const char *sh[10] = { "in","gui","fdc","net","usb","tim","acct","fl","cmp","flip" };
    int n = 0;
    for (int i = 0; i < 10 && n < cap - 40; i++) {
        kfmt(out + n, cap - n, "%s %u ", sh[i], lp_pmax[i] / cyc_ms);
        n += (int)strlen(out + n);
    }
    kfmt(out + n, cap - n, "| cmp avg %uus | %u flips gap<=%u",
         lp_cmp_n ? lp_cmp_us / lp_cmp_n : 0, lp_flips, lp_gapmax);
}

void loop_prof_reset(void)
{
    for (int i = 0; i < 10; i++) lp_pmax[i] = 0;
    lp_worst = lp_cmp_n = lp_cmp_us = lp_flips = lp_gapmax = 0;
}

static volatile u32 flight_last, flight_backoff;
static volatile int flight_want;

static void flight_worker(void)
{
    static u32 flushed;
    static u8  banner;
    for (;;) {
        if (!flight_want) { thr_yield(); continue; }
        flight_want = 0;
        if (!usb_present() || !fat_mount() || !fat_writable()) continue;

        u32 off, cnt, lost;
        u32 seq = trace_seq_get(), held = trace_held();
        if (!flight_slice(seq, held, flushed, &off, &cnt, &lost)) {
            trace_dirty = 0;
            continue;
        }
        static char fb[4300];
        int hn = 0;
        if (!banner) {
            const char *hdr = "\n--- FLOPNIX flight recorder ---\n";
            for (const char *h = hdr; *h; h++) fb[hn++] = *h;
        }
        if (lost) {
            char lm[64];
            kfmt(lm, sizeof lm, "... %u bytes lost (ring wrapped)\n", lost);
            for (const char *h = lm; *h; h++) fb[hn++] = *h;
        }
        if (cnt > (u32)(sizeof fb - hn)) cnt = sizeof fb - hn;
        int got = trace_slice_read(off, fb + hn, cnt);
        hn += got;
        if (fat_append("FLIGHT.TXT", (const u8 *)fb, (u32)hn) == 0) {
            banner = 1;
            flushed = (seq - held) + off + (u32)got;
            if (flushed >= seq) trace_dirty = 0;
            flight_backoff = 0;
        } else flight_backoff = FLIGHT_BACKOFF;
    }
}

static void autoexec_run(void)
{
    if (boot_shift) { klog("autoexec: skipped (Shift held)\n"); return; }
    int n = fs_read("autoexec", iobuf, IOBUF_SZ);
    if (n <= 0) return;

    static char script[2048];
    if (n > (int)sizeof script - 1) n = sizeof script - 1;
    memcpy(script, iobuf, n);
    script[n] = 0;
    if (win_open(WT_TERM) < 0) return;
    int pos = 0;
    char line[AX_LINE];
    while (ax_next(script, n, &pos, line, sizeof line))
        shell_exec(line);
}

static HangWatch hangw = { -1, 0, 0, 0, 0 };
static int hang_ask_win = -1;

static void hang_cb(int result, void *ctx)
{
    (void)ctx;
    int w = hang_ask_win;
    hang_ask_win = -1;
    if (w < 0) return;
    u32 el;
    if (app_stuck(&el) != w) return;
    if (result == MBR_YES) app_kill_request(w);
    else                   hw_rearm(&hangw, el);
}

int hang_stuck_win = -1;

static void hang_watch(void)
{
    if (kupd_critical) return;
    u32 el = 0;
    int w = app_stuck(&el);
    static u32 io_seen;
    u32 io_done=app_io_tick;
    if(io_seen!=io_done){io_seen=io_done;hw_io(&hangw,w,el);}
    int fired = hw_tick(&hangw, w, w >= 0 ? el : 0, app_progress());

    hang_stuck_win = (w >= 0 && hw_flagged(&hangw, w)) ? w : -1;
    if (fired && hang_ask_win < 0) {
        hang_ask_win = w;
        const Win *win = win_slot(w);
        const char *nm = win ? (win->tbuf_on ? win->tbuf : win->title)
                             : "The app";
        char text[64];
        kfmt(text, sizeof text, "%s is not responding. End the task?", nm);
        msgbox("Not responding", text, MB_YESNO, hang_cb, 0);
    }
}

void kmain(void)
{
    idt_init();

    emergency_init();
    memory = memory_layout(BOOTINFO->mem_kb);
    pic_init();
    mmx_init();

    bda_ebda_seg    = *(volatile u16 *)0x40E;
    bda_base_mem_kb = *(volatile u16 *)0x413;
    paging_init();

    ring3_init();

    {
        u32 hb = pci_cfg_read(0, 0, 0, 0);
        plat_emulated = plat_from_hostbridge(hb);
        char pb[64];
        kfmt(pb, sizeof pb, "platform: %s (host bridge %04x:%04x)\n",
             plat_emulated ? "emulated" : "real hardware",
             hb & 0xFFFF, (hb >> 16) & 0xFFFF);
        klog(pb);
    }
    gfx_init();

    outb(0x3F2, 0x0C);

    {
        char vb[96];
        kfmt(vb, sizeof vb, "FLOPNIX " OS_VER " self-check (kernel " OS_VER
             ", KAPI v%d, built " OS_BUILD_DATE ")\n", KAPI_VERSION);
        bprint(vb);
    }
    checks_active = 1;

    bprint("keyboard ");
    kbd_init();
    if (kbd_present()) bprint("ok\n"); else bfail("E11");

    bprint("mouse ");
    if (mouse_init()) bprint("ok\n"); else bfail("E12");

    bprint("timer ");
    pit_init();
    {
        int stormed = 0;
        u16 a = pit_read();
        int counting = 0;
        for (int i = 0; i < 1000 && !counting; i++)
            if (pit_read() != a) counting = 1;
        if (!counting) {
            bfail("E16");
        } else {
            bmark('p');
            outb(0x21, 0xFE);
            outb(0xA1, 0xFF);
            sti();
            bmark('i');

            u32 t0 = ticks;
            u16 prev = pit_read();
            u32 acc = 0, it = 0;

            while (ticks == t0 && acc <= 596590 && ++it <= 2000000u)
                acc += pit_step(&prev);
            timer_alive = (ticks != t0);
            if (!timer_alive) {
                bfail("E10");
            } else {
                bmark('t');
                t0 = ticks;
                prev = pit_read();
                acc = 0; it = 0;

                while (acc < 238636 && ++it <= 2000000u &&
                       (u32)(ticks - t0) < 2000)
                    acc += pit_step(&prev);
                u32 hz = (ticks - t0) * 10;
                if (hz > 1000) {
                    char c[20];
                    kfmt(c, sizeof c, "E15.0/%u", hz);
                    bfail(c);
                    stormed = 1;
                    timer_alive = 0;
                } else {
                    char b[24];
                    kfmt(b, sizeof b, " ok (%u/s)\n", hz);
                    bprint(b);
                }
            }
        }
        outb(0x21, stormed ? 0xB9 : 0xB8);
        outb(0xA1, 0xEF);
        sti();
    }

    bprint("cpu ");
    cpu_init();
    {
        char b[64];
        if (cpu_mhz()) kfmt(b, sizeof b, " ok (%s @ %u MHz)\n", cpu_brand(), cpu_mhz());
        else           kfmt(b, sizeof b, " ok (%s)\n", cpu_brand());
        bprint(b);
    }

    bprint("floppy ");
    fdc_init();
    if (fdc_ok) bprint(" ok\n"); else bfail("E20");

    bprint("config ");
    config_load();
    {
        extern u8 cfg_was_blank;
        bprint(cfg_was_blank ? "defaults\n" : "ok\n");
    }

    bprint("usb ");
    usb_init();
    {
        extern int usb_stage;
        if (usb_present()) bprint(" ok\n");
        else if (usb_stage <= 1) bprint(" none\n");
        else {
            char c[12];
            kfmt(c, sizeof c, "E30.%d", usb_stage);
            bfail(c);
        }
    }

    heap_init();
    apps_init();

    bprint("base kernel ok\n");
    kext_boot();

    hold_ms(boot_errs[0] ? 5000 : 2500);

    threads_init();
    bprint("threads ");
    {
        int ts = threads_selftest();
        if (ts == 0) {
            bprint("ok\n");
            klog("threads: switch/mutex/canary ok, preemption off\n");
        } else {
            char tb[48];
            bfail("E11");
            kfmt(tb, sizeof tb, "threads: selftest FAILED at stage %d\n", ts);
            klog(tb);
        }
    }

    thread_create(flight_worker, "flight");
    thread_create(app_worker, "appinput");
    thr_preempt_set(1);
    klog("threads: preemption ON, flight recorder on its own thread\n");

    bprint("starting GUI ");
    gui_init();
    bprint("ok\n");
    checks_active = 0;

    autoexec_run();

    emergency_watch_start();
    u32 last_flip = 0;

    u32 lp_mhz = cpu_mhz(); if (!lp_mhz) lp_mhz = 100;
    u32 lp_cyc_ms  = lp_mhz * 1000;
    u32 lp_thresh  = lp_cyc_ms * 100;
    u32 lp_last_log = 0;

    for (;;) {
        emergency_heartbeat();
        int work = 0;
        u8 sc;
        u32 pk, mwhen;
        u32 ts[11]; int nts = 0;
        ts[nts++] = cpu_now();
        while (kbd_pop(&sc)) { work = 1; handle_sc(sc); }
        while (mouse_pop(&pk, &mwhen)) {
            work = 1;
            u8 b0 = pk, b1 = pk >> 8, b2 = pk >> 16, b3 = pk >> 24;
            int dx = b1 - ((b0 & 0x10) ? 256 : 0);
            int dy = b2 - ((b0 & 0x20) ? 256 : 0);
            gui_mouse(dx, -dy, b0 & 7, mwhen);
            if (b3) {
                int dz = (i8)b3;
                gui_wheel(-dz);
            }
        }
        ts[nts++] = cpu_now();
        gui_tick();      ts[nts++] = cpu_now();
        fdc_tick();      ts[nts++] = cpu_now();
        net_poll();      ts[nts++] = cpu_now();
        usb_poll();      ts[nts++] = cpu_now();
        timers_poll();   ts[nts++] = cpu_now();
        static u32 last_cpu;
        if ((u32)(ticks - last_cpu) >= 100) { last_cpu = ticks; cpu_snapshot(); }
        hang_watch();
        ts[nts++] = cpu_now();

        if (flight_due(ticks, flight_last, flight_backoff, trace_dirty, gui_dirty)) {
            flight_last = ticks;
            flight_want = 1;
        }
        ts[nts++] = cpu_now();

        int drew = flip_due(ticks, &last_flip, timer_alive);
        if (drew) {
            static FrameState fs_last;
            FrameState fs_now;
            gui_frame_state(&fs_now);
            if (!frame_present(&fs_now, &fs_last)) drew = 0;

            else if (!present_try()) drew = 0;
            else fs_last = fs_now;
        }
        if (drew) { gui_dirty = 0; preempt_disable(); gui_compose(); preempt_enable(); }
        ts[nts++] = cpu_now();
        if (drew) { flip(); present_done(); }
        ts[nts++] = cpu_now();
        if (drew) {
            static u32 lp_lastflip;
            lp_cmp_us += (ts[9] - ts[8]) / lp_mhz;

            lp_cmp_n++;
            if (lp_flips) {
                u32 g = ticks - lp_lastflip;
                if (g > lp_gapmax) lp_gapmax = g;
            }
            lp_lastflip = ticks; lp_flips++;
        }

        u32 total = ts[10] - ts[0];
        u32 pworst = 0; int pi = 0;
        for (int i = 0; i < 10; i++) {
            u32 d = ts[i + 1] - ts[i];
            if (d > lp_pmax[i]) lp_pmax[i] = d;
            if (d > pworst) { pworst = d; pi = i; }
        }
        if (total > lp_worst) lp_worst = total;

        if (total > lp_thresh && (u32)(ticks - lp_last_log) >= 50) {
            lp_last_log = ticks;
            char m[80];
            kfmt(m, sizeof m, "loop stall: %ums total, %s %ums (worst %ums)",
                 total / lp_cyc_ms, lp_name[pi], pworst / lp_cyc_ms,
                 lp_worst / lp_cyc_ms);
            klog(m); klog("\n");
        }

        (void)work;
    }
}
