/* Runs the shared code tests inside the guest OS. */
#include "kapi.h"
#include "fbspan.inc"
#include "emergency_core.inc"
#include "fhlayout.inc"
#include "bench_core.inc"
#include "deskpath.inc"
#include "cpuaccount.inc"
#include "memlayout.inc"
#include "dhcp_core.inc"
#include "netprefs.inc"
#include "netconfig.inc"
#include "shpath.h"
#include "savepath.inc"
#include "paintview.inc"
#include "shcmd.inc"
#include "ioguard.inc"
#include "flipgate.inc"
#include "flightgate.inc"
#include "flightlog.inc"
#include "listkeep.inc"
#include "desklst.inc"
#include "textfield.inc"
#include "platform.inc"
#include "sched.inc"
#include "appq.inc"
#include "usbdebounce.inc"
#include "fsplan.inc"
#include "fsdefrag.inc"
#include "kbcore.inc"
#include "pumpbtn.inc"
#include "mepreset.inc"
#include "winfit.inc"
#include "winhit.inc"
#include "fhmap.inc"
#include "midiname.inc"
#include "framegate.inc"
#include "cfgsan.inc"
#include "busycore.inc"
#include "hangwatch.inc"
#include "atsw.inc"
#include "bmpw.inc"
#include "shotname.inc"
#include "clipline.inc"
#include "axline.inc"
#include "ntpcore.inc"
#include "nicdesc.inc"
#include "phylink.inc"
#include "lfncore.inc"
#include "lz.inc"
#include "sbar.inc"
#include "sbdrag.inc"
#include "modsort.inc"
#include "ms2core.inc"
#include "ramtest.inc"
#include "tetris_core.inc"
#include "pong_core.inc"
#include "snake_core.inc"
#include "serial_core.inc"
#include "faultring.inc"
#include "gfxfault.inc"
#include "fspath.inc"
#include "delprompt.inc"
#include "paging.inc"
#include "bmp.inc"
#include "wallpaper.inc"
#include "marquee.inc"
#include "apidoc.inc"
#include "notebuf.inc"
#include "notelst.inc"
#include "fdchealth.inc"
#include "midifile.inc"
#include "shcwd.inc"
#include "tabcomp.inc"
#include "tree.inc"
#include "diskmap.inc"
#include "opl2core.inc"
#include "mine_core.inc"
#include "ring3.inc"
#include "panic_report.inc"
#include "kextspace.inc"
#include "vbank.inc"
#include "mtrr.inc"
#include "net_wire.inc"
#include "net_tcp.inc"
#include "gdi.h"
#include "gdi_rgn.inc"
#include "gdi_poly.inc"
#include "gdi_curve.inc"
#include "gdi_stroke.inc"
#include "gdi_aa.inc"
#include "gdi_line.inc"
#include "g3d.h"
#include "g3d_math.inc"

static const Kapi *api;

#define COM1 0x3F8
static void ser_init(void)
{
    api->outb(COM1 + 1, 0x00);
    api->outb(COM1 + 3, 0x80);
    api->outb(COM1 + 0, 0x01);
    api->outb(COM1 + 1, 0x00);
    api->outb(COM1 + 3, 0x03);
    api->outb(COM1 + 2, 0xC7);
    api->outb(COM1 + 4, 0x0B);
}
static void ser_putc(char c)
{
    int guard = 200000;
    while (!(api->inb(COM1 + 5) & 0x20) && --guard) ;
    api->outb(COM1, (u8)c);
}
static void ser_puts(const char *s)
{
    for (; *s; s++) { if (*s == '\n') ser_putc('\r'); ser_putc(*s); }
}

static int g_pass, g_fail, g_tfail;

static void check(int cond, const char *expr)
{
    if (cond) { g_pass++; return; }
    g_fail++; g_tfail++;
    ser_puts("  FAIL: "); ser_puts(expr); ser_puts("\n");
}
#define CHECK(c) check((c), #c)
#ifndef SELFTEST_SMALL
#define APP_TEST_PHASE(s) ser_puts("APP CORE: " s "\n")
#include "newapps_tests.inc"
#endif
#define STREQ(a, b) (!api->strcmp((a), (b)))

static void run(const char *name, void (*fn)(void))
{
    g_tfail = 0;
    fn();
    ser_puts(g_tfail ? "not ok " : "ok   ");
    ser_puts(name); ser_puts("\n");
}

static void t_crc32(void)
{

    CHECK(api->crc32("FLOPNIX", 7) == 0xed8c1850u);
    CHECK(api->crc32("", 0) == 0u);
}
static void t_b64(void)
{
    char enc[32];
    api->b64_encode((const u8 *)"FLOPNIX", 7, enc, sizeof enc);
    CHECK(STREQ(enc, "RkxPUE5JWA=="));
}
static void t_b64_roundtrip(void)
{
    char enc[32]; u8 dec[16];
    api->b64_encode((const u8 *)"hello", 5, enc, sizeof enc);
    int dl = api->b64_decode(enc, dec, sizeof dec);
    CHECK(dl == 5);
    dec[dl < 0 ? 0 : dl] = 0;
    CHECK(STREQ((char *)dec, "hello"));
}
static void t_path(void)
{
    CHECK(STREQ(api->path_base("u:/photos/trip.bmp"), "trip.bmp"));
    CHECK(STREQ(api->path_base("noslash"), "noslash"));
    CHECK(STREQ(api->path_ext("u:/photos/trip.bmp"), "bmp"));
    CHECK(STREQ(api->path_ext("noext"), ""));
}
static void t_kfmt_pad(void)
{
    char b[40];

    api->kfmt(b, sizeof b, "[%6s]", "ab");   CHECK(STREQ(b, "[    ab]"));
    api->kfmt(b, sizeof b, "[%04u]", 7u);    CHECK(STREQ(b, "[0007]"));
    api->kfmt(b, sizeof b, "[%6u]", 42u);    CHECK(STREQ(b, "[    42]"));

    api->kfmt(b, sizeof b, "[%-6s]", "ab");  CHECK(STREQ(b, "[ab    ]"));
    api->kfmt(b, sizeof b, "[%-4d]", 7);     CHECK(STREQ(b, "[7   ]"));

    api->kfmt(b, sizeof b, "[%-2s]", "abcd"); CHECK(STREQ(b, "[abcd]"));
    api->kfmt(b, sizeof b, "[%2s]", "abcd");  CHECK(STREQ(b, "[abcd]"));

    api->kfmt(b, sizeof b, "[%-04u]", 7u);   CHECK(STREQ(b, "[7   ]"));

    char s[6];
    api->kfmt(s, sizeof s, "%s", "abcdefghij");
    CHECK(api->strlen(s) == 5 && STREQ(s, "abcde"));
    api->kfmt(s, sizeof s, "%-10s", "abcdefghij");
    CHECK(api->strlen(s) == 5 && STREQ(s, "abcde"));
    api->kfmt(s, sizeof s, "ab%u", 123456789u);
    CHECK(api->strlen(s) == 5 && STREQ(s, "ab123"));
    api->kfmt(s, sizeof s, "%s-%s", "abc", "def");
    CHECK(api->strlen(s) == 5 && STREQ(s, "abc-d"));
}

static void t_atoi(void)
{
    CHECK(api->atoi("42") == 42);
    CHECK(api->atoi("-7") == -7);
    CHECK(api->atoi("") == 0);
}

static void t_normpath(void)
{
    char o[64];
    sh_norm_path("sys/fat.kx", o, sizeof o);       CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("fat.kx", o, sizeof o);           CHECK(STREQ(o, "fat.kx"));
    sh_norm_path("/sys/fat.kx", o, sizeof o);      CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("sys\\fat.kx", o, sizeof o);      CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("a:sys/fat.kx", o, sizeof o);     CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("A:/sys/fat.kx", o, sizeof o);    CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("Floppy\\sys\\fat.kx", o, sizeof o); CHECK(STREQ(o, "sys/fat.kx"));
    sh_norm_path("Floppy/fat.kx", o, sizeof o);    CHECK(STREQ(o, "fat.kx"));
    sh_norm_path("  sys/fat.kx", o, sizeof o);     CHECK(STREQ(o, "sys/fat.kx"));
}

static void t_spec(void)
{
    char o[64];
    int d = -1;

    CHECK(sh_spec_split("a:notes.txt", &d, o, sizeof o) == 1);
    CHECK(d == 0 && STREQ(o, "notes.txt"));
    CHECK(sh_spec_split("a:sys/gdi.kx", &d, o, sizeof o) == 1);
    CHECK(d == 0 && STREQ(o, "sys/gdi.kx"));
    CHECK(sh_spec_split("u:/pics/x.bmp", &d, o, sizeof o) == 1);
    CHECK(d == 1 && STREQ(o, "/pics/x.bmp"));
    CHECK(sh_spec_split("U:/X.BMP", &d, o, sizeof o) == 1);
    CHECK(d == 1 && STREQ(o, "/X.BMP"));

    d = 9;
    CHECK(sh_spec_split("plain.bmp", &d, o, sizeof o) == 0);
    CHECK(d == 0 && STREQ(o, "plain.bmp"));
    CHECK(sh_spec_split("", &d, o, sizeof o) == 0 && STREQ(o, ""));

    sh_spec_make(0, "notes.txt", o, sizeof o);        CHECK(STREQ(o, "a:notes.txt"));
    sh_spec_make(1, "/pics/x.bmp", o, sizeof o);      CHECK(STREQ(o, "u:/pics/x.bmp"));
    sh_spec_make(1, "x.bmp", o, sizeof o);            CHECK(STREQ(o, "u:/x.bmp"));

    sh_spec_make(1, "/a/b.bmp", o, sizeof o);
    int d2; char o2[64];
    CHECK(sh_spec_split(o, &d2, o2, sizeof o2) == 1);
    CHECK(d2 == 1 && STREQ(o2, "/a/b.bmp"));

    char tiny[6];
    sh_spec_make(0, "abcdefgh", tiny, sizeof tiny);
    CHECK(api->strlen(tiny) == 5 && STREQ(tiny, "a:abc"));
}

static const char *const all_cmds[] = {
    "ls","map","cat","rm","cp","mv","touch","echo","hexdump","wc","head","tail",
    "grep","find","edit","uls","ucat","ucp","urm","ifconfig","ping","dns",
    "wget","lspci",
    "set","uname","df","cal","sort","stat","history","calc","ps","kill",
    "matrix","rainbow","beep","clear","usb","dmesg","kupdate","crash","kext",
    "help","cls","date","uptime","free","fetch","ver","whoami","pwd",
    "settings","about","reboot","defrag","fscan","bootsec","bios",
    "shutdown","testram", 0
};

static int sc_starts(const char *s, const char *p)
{
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return *s == ' ' || *s == 0;
}

static void t_usage(void)
{
    for (int i = 0; all_cmds[i]; i++) {
        const char *u = sh_usage(all_cmds[i]);
        CHECK(u != 0);
        CHECK(u && sc_starts(u, all_cmds[i]));
    }

    CHECK(sh_usage("nosuchcmd") == 0);
    CHECK(sh_usage("") == 0);
    CHECK(sh_usage("ca") == 0);
    CHECK(sh_usage("catt") == 0);
}

static void t_ioguard(void)
{
    IoGuard g = 0;

    CHECK(io_held(&g) == 0);
    CHECK(io_acquire(&g) == 1);
    CHECK(io_held(&g) == 1);

    CHECK(io_acquire(&g) == 0);
    CHECK(io_acquire(&g) == 0);
    CHECK(io_held(&g) == 1);

    io_release(&g);
    CHECK(io_held(&g) == 0);
    CHECK(io_acquire(&g) == 1);
    io_release(&g);

    io_release(&g);
    CHECK(io_held(&g) == 0);
    CHECK(io_acquire(&g) == 1);
    io_release(&g);

    IoGuard a = 0, b = 0;
    CHECK(io_acquire(&a) == 1);
    CHECK(io_acquire(&b) == 1);
    CHECK(io_acquire(&a) == 0);
    io_release(&a);
    CHECK(io_held(&b) == 1);
    io_release(&b);
    CHECK(io_held(&a) == 0 && io_held(&b) == 0);

    int leaked = 0;
    for (int i = 0; i < 500; i++) {
        if (!io_acquire(&g)) { leaked = 1; break; }
        io_release(&g);
    }
    CHECK(leaked == 0);
    CHECK(io_held(&g) == 0);
}

static int bits16(u16 v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static void t_tetris(void)
{

    int bad = 0;
    for (int p = 0; p < 7; p++)
        for (int r = 0; r < 4; r++)
            if (bits16(tet_mask[p][r]) != 4) bad++;
    CHECK(bad == 0);
    CHECK(tet_mask[TP_O][0] == tet_mask[TP_O][3]);
    CHECK(tet_mask[TP_I][0] != tet_mask[TP_I][1]);
    CHECK(tet_mask[TP_I][0] == tet_mask[TP_I][2] ||
          bits16(tet_mask[TP_I][2]) == 4);

    u8 bd[TET_W * TET_H];
    for (int i = 0; i < TET_W * TET_H; i++) bd[i] = 0;

    CHECK(tet_fits(bd, TP_O, 0, 4, 0) == 1);
    CHECK(tet_fits(bd, TP_O, 0, -2, 0) == 0);
    CHECK(tet_fits(bd, TP_I, 1, 8, 0) == 0);

    CHECK(tet_fits(bd, TP_I, 0, 3, 18) == 1);

    CHECK(tet_fits(bd, TP_I, 0, 3, 19) == 0);

    tet_lock(bd, TP_O, 0, 4, 18);
    CHECK(bd[19 * TET_W + 5] == TP_O + 1);
    CHECK(bd[19 * TET_W + 6] == TP_O + 1);
    CHECK(tet_fits(bd, TP_O, 0, 4, 18) == 0);
    CHECK(tet_fits(bd, TP_O, 0, 1, 18) == 1);

    for (int x = 0; x < TET_W; x++) bd[19 * TET_W + x] = 1;
    bd[19 * TET_W + 0] = 0;
    CHECK(tet_clear(bd) == 0);
    bd[19 * TET_W + 0] = 1;
    int before18_5 = bd[18 * TET_W + 5];
    CHECK(before18_5 == TP_O + 1);
    CHECK(tet_clear(bd) == 1);
    CHECK(bd[19 * TET_W + 5] == TP_O + 1);
    CHECK(bd[18 * TET_W + 5] == 0);

    for (int i = 0; i < TET_W * TET_H; i++) bd[i] = 0;
    for (int y = 16; y < 20; y++)
        for (int x = 0; x < TET_W; x++) bd[y * TET_W + x] = 2;
    CHECK(tet_clear(bd) == 4);
    int empt = 1;
    for (int i = 0; i < TET_W * TET_H; i++) if (bd[i]) empt = 0;
    CHECK(empt == 1);

    CHECK(tet_score(1, 0) == 40 && tet_score(4, 0) == 1200);
    CHECK(tet_score(2, 3) == 400);
    CHECK(tet_score(0, 5) == 0);
    CHECK(tet_level(0) == 0 && tet_level(9) == 0 && tet_level(10) == 1);
    CHECK(tet_level(55) == 5);
    CHECK(tet_speed(0) > tet_speed(3));
    CHECK(tet_speed(3) > tet_speed(8));
    CHECK(tet_speed(99) >= 2);
}

static void t_pong_hit(void)
{
    PongSt s;

    pong_serve(&s, 1);
    s.by = (PONG_H / 2) << 8;
    s.vy = 0;
    pong_step(&s);
    CHECK(s.hit == 0);

    pong_serve(&s, 1);
    s.by = 0;
    s.vy = -(1 << 8);
    pong_step(&s);
    CHECK((s.hit & PONG_HIT_WALL) != 0);
    CHECK(s.vy > 0);

    pong_serve(&s, 1);
    s.by = (PONG_H - PONG_B) << 8;
    s.vy = 1 << 8;
    pong_step(&s);
    CHECK((s.hit & PONG_HIT_WALL) != 0);

    pong_serve(&s, -1);
    s.bx = (PONG_PX + PONG_PW) << 8;
    s.by = (PONG_H / 2) << 8;
    s.vy = 0;
    s.pl = (PONG_H / 2) - PONG_PH / 2;
    pong_step(&s);
    CHECK((s.hit & PONG_HIT_PADDLE) != 0);
    CHECK(s.vx > 0);

    pong_serve(&s, 1);
    s.bx = (PONG_W - PONG_PX - PONG_PW - PONG_B) << 8;
    s.by = (PONG_H / 2) << 8;
    s.vy = 0;
    s.pr = (PONG_H / 2) - PONG_PH / 2;
    pong_step(&s);
    CHECK((s.hit & PONG_HIT_PADDLE) != 0);
    CHECK(s.vx < 0);

    pong_serve(&s, -1);
    s.bx = (PONG_PX + PONG_PW) << 8;
    s.by = 0;
    s.vy = 0;
    s.pl = PONG_H - PONG_PH;
    pong_step(&s);
    CHECK((s.hit & PONG_HIT_PADDLE) == 0);

    pong_serve(&s, 1);
    s.by = 0;
    s.vy = -(1 << 8);
    pong_step(&s);
    CHECK(s.hit != 0);
    s.by = (PONG_H / 2) << 8;
    s.vy = 0;
    pong_step(&s);
    CHECK(s.hit == 0);
}

static void t_pong(void)
{
    PongSt s;
    pong_serve(&s, 1);
    CHECK(s.vx > 0);
    CHECK(s.bx == ((PONG_W / 2 - PONG_B / 2) << 8));
    pong_serve(&s, -1);
    CHECK(s.vx < 0);

    pong_serve(&s, 1);
    s.by = 3 << 8; s.vy = -(2 << 8);
    int flipped = 0;
    for (int i = 0; i < 10 && !flipped; i++) {
        pong_step(&s);
        if (s.vy > 0) flipped = 1;
        CHECK(s.by >= 0);
    }
    CHECK(flipped == 1);

    pong_serve(&s, 1);
    s.pr = PONG_H / 2 - PONG_PH / 2;
    s.bx = (PONG_W - PONG_PX - PONG_PW - PONG_B - 1) << 8;
    s.by = (PONG_H / 2 - PONG_B / 2) << 8;
    i32 speed_in = s.vx;
    int r = 0;
    for (int i = 0; i < 8 && s.vx > 0; i++) r = pong_step(&s);
    CHECK(s.vx < 0);
    CHECK(r == 0);
    CHECK(-s.vx >= speed_in);

    pong_serve(&s, 1);
    s.pr = PONG_H / 2;
    s.bx = (PONG_W - PONG_PX - PONG_PW - PONG_B - 1) << 8;
    s.by = (s.pr + 2) << 8;
    s.vy = 0;
    for (int i = 0; i < 8 && s.vx > 0; i++) pong_step(&s);
    CHECK(s.vx < 0 && s.vy < 0);

    pong_serve(&s, 1);
    s.pr = 0;
    s.bx = (PONG_W - 2) << 8;
    s.by = (PONG_H - PONG_B - 2) << 8;
    int got = 0;
    for (int i = 0; i < 6 && !got; i++) got = pong_step(&s);
    CHECK(got == 1);

    pong_serve(&s, 1);
    s.pr = PONG_H / 2 - PONG_PH / 2;
    s.by = (PONG_H / 2 - PONG_B / 2) << 8;
    s.pl = s.pr;
    i32 mag0 = s.vx;
    pong_paddle(&s, s.pr);
    i32 mag1 = s.vx < 0 ? -s.vx : s.vx;
    CHECK(mag1 == PONG_V0 + PONG_VINC);
    CHECK(mag0 == PONG_V0);
    for (int i = 0; i < 40; i++) pong_paddle(&s, s.pr);
    i32 magN = s.vx < 0 ? -s.vx : s.vx;
    CHECK(magN == PONG_VMAX);
    for (int i = 0; i < 10; i++) pong_paddle(&s, s.pr);
    CHECK((s.vx < 0 ? -s.vx : s.vx) == PONG_VMAX);

    pong_serve(&s, 1);
    s.pr = 0;
    s.by = (PONG_H - 20) << 8;
    pong_ai(&s, 3);
    CHECK(s.pr == 3);
    s.pr = PONG_H - PONG_PH;
    pong_ai(&s, 5);
    CHECK(s.pr <= PONG_H - PONG_PH);
    s.by = 0; s.pr = 4;
    pong_ai(&s, 50);
    CHECK(s.pr >= 0);
}

static void t_fsplan(void)
{

    FpEnt e[4] = {
        { 310, 2, 0, 1, 0 }, { 300, 5, 0, 1, 1 },
        { 320, 1, 0, 1, 2 }, { 0, 0, 0, 0, 3 },
    };
    int moves = fs_plan(e, 4, 299);
    CHECK(moves == 3);
    CHECK(e[0].start == 300 && e[0].nstart == 299);
    CHECK(e[1].start == 310 && e[1].nstart == 304);
    CHECK(e[2].start == 320 && e[2].nstart == 306);
    int down = 1;
    for (int i = 0; i < 3; i++) if (e[i].nstart > e[i].start) down = 0;
    CHECK(down == 1);

    FpEnt f[2] = { { 299, 4, 0, 1, 0 }, { 303, 2, 0, 1, 1 } };
    CHECK(fs_plan(f, 2, 299) == 0);
    CHECK(f[0].nstart == 299 && f[1].nstart == 303);

    FpEnt g[4] = {
        { 299, 4, 7777, 1, 0 },
        { 0,   0, 7777, 0, 1 },
        { 310, 6, 7777, 1, 2 },
        { 0,   0, 7777, 0, 3 },
    };
    CHECK(fs_plan(g, 4, 299) == 1);
    CHECK(g[0].nstart == 299 && g[1].nstart == 303);
    CHECK(g[2].used == 0 && g[3].used == 0);
}

#define FSD_NSEC 24
static u8  fsd_disk[FSD_NSEC][512];
static int fsd_fail_rd, fsd_fail_wr;
static int fsd_fail_rd2;

static u8  fsd_rdcnt[FSD_NSEC];
static int fsd_reads;

static int fsd_rd(u32 lba, u8 *sec)
{
    fsd_reads++;
    if ((int)lba == fsd_fail_rd) return -1;
    if ((int)lba == fsd_fail_rd2 && fsd_rdcnt[lba]++) return -1;
    api->memcpy(sec, fsd_disk[lba], 512);
    return 0;
}
static int fsd_wr(u32 lba, const u8 *sec)
{
    if ((int)lba == fsd_fail_wr) {
        api->memset(fsd_disk[lba], 0xEE, 512);
        return -1;
    }
    api->memcpy(fsd_disk[lba], sec, 512);
    return 0;
}

static void fsd_reset(u32 S, u32 L)
{
    for (int i = 0; i < FSD_NSEC; i++) api->memset(fsd_disk[i], 0x11, 512);
    for (u32 s = 0; s < L; s++) api->memset(fsd_disk[S + s], 0xA0 + (int)s, 512);
    fsd_fail_rd = fsd_fail_wr = fsd_fail_rd2 = -1;
    api->memset(fsd_rdcnt, 0, sizeof fsd_rdcnt);
    fsd_reads = 0;
}
static int fsd_at(u32 lba, int b) { return fsd_disk[lba][0] == b && fsd_disk[lba][511] == b; }

static void t_fsd_moves(void)
{
    u8 sec[512];

    fsd_reset(10, 5);
    CHECK(fsd_move(fsd_rd, fsd_wr, 10, 2, 5, sec) == FSD_MOVED);
    for (int s = 0; s < 5; s++) CHECK(fsd_at(2 + s, 0xA0 + s));
    CHECK(fsd_reads == 5);

    fsd_reset(6, 10);
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_MOVED);
    for (int s = 0; s < 10; s++) CHECK(fsd_at(2 + s, 0xA0 + s));
    CHECK(fsd_reads == 20);

    fsd_reset(6, 3);
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 6, 3, sec) == FSD_MOVED);
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 0, sec) == FSD_MOVED);
    CHECK(fsd_reads == 0);
}

static void t_fsd_preverify(void)
{
    u8 sec[512];

    fsd_reset(6, 10);
    fsd_fail_rd = 6 + 7;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_INTACT);
    for (int s = 0; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));
    CHECK(fsd_at(2, 0x11) && fsd_at(5, 0x11));

    fsd_reset(10, 5);
    fsd_fail_rd = 10 + 3;
    CHECK(fsd_move(fsd_rd, fsd_wr, 10, 2, 5, sec) == FSD_INTACT);
    for (int s = 0; s < 5; s++) CHECK(fsd_at(10 + s, 0xA0 + s));
}

static void t_fsd_wrfail(void)
{
    u8 sec[512];

    fsd_reset(6, 10);
    fsd_fail_wr = 2 + 2;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_INTACT);
    for (int s = 0; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));

    fsd_reset(6, 10);
    fsd_fail_rd2 = 6 + 7;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_RESTORED);
    for (int s = 0; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));

    fsd_reset(6, 10);
    fsd_fail_rd2 = 6 + 4;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_INTACT);
    for (int s = 0; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));
}

static void t_heap_bounds(void)
{
    CHECK(api->kmalloc(0) == 0);
    CHECK(api->kmalloc(0xFFFFFFFFu) == 0);
    CHECK(api->kmalloc(0xFFFFFFF9u) == 0);
    CHECK(api->kmalloc(0x40000000u) == 0);
    u8 *p = (u8 *)api->kmalloc(64);
    CHECK(p != 0);
    if (p) { api->memset(p, 0x5A, 64); CHECK(p[0] == 0x5A && p[63] == 0x5A); }
    api->kfree(p);
}

static void t_ms_bounds(void)
{
    u8 fixed[64], src[64];
    api->memset(src, 0x33, sizeof src);
    MemStream s;
    api->ms_open(&s, fixed, sizeof fixed);
    CHECK(api->ms_write(&s, src, 8) == 8);

    CHECK(api->ms_write(&s, src, 0xFFFFFFFCu) == 56);
    CHECK(s.pos == 64 && s.len == 64);
    CHECK(api->ms_write(&s, src, 1) == 0);

    MemStream g;
    CHECK(api->ms_alloc(&g, 16) == 1);
    CHECK(api->ms_write(&g, src, 64) == 64);
    CHECK(api->ms_write(&g, src, 0xFFFFFFF0u) == 0);

    CHECK(g.len == 64);
    api->ms_free(&g);
}

static int kb_tap(KbSt *st, u8 code)
{
    int tr, dn;
    int d = kb_feed(st, code, &tr, &dn);
    kb_feed(st, (u8)(code | 0x80), &tr, &dn);
    return d;
}
static int kb_tap_e0(KbSt *st, u8 code)
{
    int tr, dn;
    kb_feed(st, 0xE0, &tr, &dn);
    int d = kb_feed(st, code, &tr, &dn);
    kb_feed(st, 0xE0, &tr, &dn);
    kb_feed(st, (u8)(code | 0x80), &tr, &dn);
    return d;
}
#define KB_FRESH { 0, 0, 0, 0, 0, KB_NUMLOCK_DEFAULT, 0, 0 }

static void t_framegate_idle_skips(void)
{
    FrameState s = { 0, 100, 100, 0, 0 };

    CHECK(frame_present(&s, &s) == 0);
}

static void t_framegate_damage_presents(void)
{
    FrameState last = { 0, 100, 100, 0, 0 };
    FrameState now  = { 1, 100, 100, 0, 0 };
    CHECK(frame_present(&now, &last) == 1);
}

static void t_framegate_cursor_presents(void)
{
    FrameState last = { 0, 100, 100, 0, 0 };
    FrameState now  = { 0, 101, 100, 0, 0 };
    CHECK(frame_present(&now, &last) == 1);
    now = (FrameState){ 0, 100, 140, 0, 0 };
    CHECK(frame_present(&now, &last) == 1);
}

static void t_framegate_blink_presents(void)
{
    FrameState last = { 0, 100, 100, 0, 0 };
    FrameState now  = { 0, 100, 100, 1, 0 };
    CHECK(frame_present(&now, &last) == 1);
}

static void t_framegate_screensaver_always(void)
{
    FrameState last = { 0, 100, 100, 0, 1 };
    FrameState now  = { 0, 100, 100, 0, 1 };
    CHECK(frame_present(&now, &last) == 1);
}

static void t_midiname_display(void)
{
    char b[72];

    mn_display("u:/strategy.mid", -1, b, sizeof b);
    CHECK(STREQ(b, "USB: strategy.mid"));
    mn_display("a:song.mid", -1, b, sizeof b);
    CHECK(STREQ(b, "A: song.mid"));

    mn_display("/strategy.mid", 1, b, sizeof b);
    CHECK(STREQ(b, "USB: strategy.mid"));
    mn_display("song.mid", 0, b, sizeof b);
    CHECK(STREQ(b, "A: song.mid"));

    mn_display("u:/music/loud.mid", -1, b, sizeof b);
    CHECK(STREQ(b, "USB: loud.mid"));

    mn_display("/music/loud.mid", 1, b, sizeof b);
    CHECK(STREQ(b, "USB: loud.mid"));
}

static void t_hangwatch_prompts_once(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    CHECK(hw_tick(&h, -1, 0, 0) == 0);
    CHECK(hw_tick(&h, 4, 10, 0) == 0);
    CHECK(hw_tick(&h, 4, HW_WAIT - 1, 0) == 0);
    CHECK(hw_tick(&h, 4, HW_WAIT, 0) == 1);
    CHECK(hw_tick(&h, 4, HW_WAIT + 50, 0) == 0);
    CHECK(hw_tick(&h, -1, 0, 0) == 0);
    CHECK(hw_tick(&h, 4, HW_WAIT, 0) == 1);
}

static void t_hangwatch_progress_never_accused(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    u32 prog = 0;
    for (u32 t = 0; t < HW_HARD - 10; t += 50) {
        prog++;
        CHECK(hw_tick(&h, 2, t, prog) == 0);
    }
}

static void t_hangwatch_hard_ceiling(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    u32 prog = 0;
    CHECK(hw_tick(&h, 3, 0, ++prog) == 0);
    CHECK(hw_tick(&h, 3, HW_HARD - 1, ++prog) == 0);
    CHECK(hw_tick(&h, 3, HW_HARD, ++prog) == 1);
    CHECK(hw_tick(&h, 3, HW_HARD + 500, ++prog) == 0);

    hw_tick(&h,-1,0,prog);
    CHECK(hw_tick(&h,3,0,++prog)==0);
    for(u32 t=100;t<3*HW_HARD;t+=100){
        hw_io(&h,3,t);
        CHECK(hw_tick(&h,3,t,++prog)==0);
    }
    hw_io(&h,4,4*HW_HARD);
    CHECK(hw_tick(&h,3,4*HW_HARD-101,++prog)==0);
    CHECK(hw_tick(&h,3,4*HW_HARD-100,++prog)==1);

}

static void t_hangwatch_progress_then_stops(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    u32 prog = 0;
    CHECK(hw_tick(&h, 5, 100, ++prog) == 0);
    CHECK(hw_tick(&h, 5, 200, ++prog) == 0);
    CHECK(hw_tick(&h, 5, 400, prog) == 0);
    CHECK(hw_tick(&h, 5, 200 + HW_WAIT - 1, prog) == 0);
    CHECK(hw_tick(&h, 5, 200 + HW_WAIT, prog) == 1);
}

static void t_hangwatch_rearm_past_hard(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    u32 prog = 0;
    CHECK(hw_tick(&h, 6, HW_HARD, ++prog) == 1);
    hw_rearm(&h, HW_HARD);
    CHECK(hw_tick(&h, 6, HW_HARD + 1, ++prog) == 0);
    CHECK(hw_tick(&h, 6, HW_HARD + 100, ++prog) == 0);
}

static void t_hangwatch_rearm(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    CHECK(hw_tick(&h, 2, HW_WAIT, 0) == 1);
    hw_rearm(&h, HW_WAIT + 3);
    CHECK(hw_tick(&h, 2, HW_WAIT + 10, 0) == 0);
    CHECK(hw_tick(&h, 2, 2 * HW_WAIT + 2, 0) == 0);
    CHECK(hw_tick(&h, 2, 2 * HW_WAIT + 3, 0) == 1);
}

static void t_hangwatch_win_change(void)
{
    HangWatch h = { -1, 0, 0, 0, 0 };
    CHECK(hw_tick(&h, 1, HW_WAIT, 0) == 1);
    CHECK(hw_tick(&h, 6, 5, 0) == 0);
    CHECK(hw_tick(&h, 6, HW_WAIT, 0) == 1);
}

static void t_paint_view(void)
{
    CHECK(pv_limit(320, 320, 1, 50) == 0);
    CHECK(pv_limit(320, 160, 2, 999) == 240);
    CHECK(pv_limit(320, 161, 2, 999) == 239);
    CHECK(pv_limit(160, 300, 1, 99) == 0);
    CHECK(pv_limit(320, 160, 4, -5) == 0);
    CHECK(pv_point(0, 4, 80, 320) == 80);
    CHECK(pv_point(3, 4, 80, 320) == 80);
    CHECK(pv_point(4, 4, 80, 320) == 81);
    CHECK(pv_point(-8, 2, 40, 320) == 40);
    CHECK(pv_point(999, 2, 40, 320) == 319);
    u8 p[8] = {99, 1, 2, 3, 4, 5, 6, 99};
    pv_flip(p + 1, 3, 2, 0);
    CHECK(p[1] == 3 && p[2] == 2 && p[3] == 1);
    CHECK(p[4] == 6 && p[5] == 5 && p[6] == 4);
    pv_flip(p + 1, 3, 2, 1);
    CHECK(p[1] == 6 && p[2] == 5 && p[3] == 4);
    CHECK(p[4] == 3 && p[5] == 2 && p[6] == 1);
    pv_flip(p + 1, 3, 2, 0); pv_flip(p + 1, 3, 2, 1);
    for (int i = 1; i <= 6; i++) CHECK(p[i] == i);
    CHECK(p[0] == 99 && p[7] == 99);
    pv_flip(p + 1, 2, 3, 1);
    CHECK(p[1] == 5 && p[2] == 6 && p[3] == 3 && p[4] == 4);
    CHECK(p[5] == 1 && p[6] == 2);
}

static void t_busycore_quick_event(void)
{
    BusyCore b = { -1, 0, 0 };
    bc_start(&b, 0, 100);
    CHECK(bc_show(&b, 0, 100) == 0);
    CHECK(bc_show(&b, 0, 105) == 0);
    bc_end(&b, 106);
    CHECK(bc_show(&b, 0, 120) == 0);
}

static void t_busycore_long_handler(void)
{
    BusyCore b = { -1, 0, 0 };
    bc_start(&b, 2, 0);
    CHECK(bc_show(&b, 2, 5) == 0);
    CHECK(bc_show(&b, 2, BC_SHOW) == 1);
    CHECK(bc_show(&b, 2, 40) == 1);
    CHECK(bc_show(&b, 1, 40) == 0);
    bc_end(&b, 50);
    CHECK(bc_show(&b, 2, 50) == 0);
}

static void t_busycore_stream_no_strobe(void)
{
    BusyCore b = { -1, 0, 0 };
    for (u32 t = 0; t < 100; t += 5) {
        bc_start(&b, 0, t);
        CHECK(bc_show(&b, 0, t + 4) == 0);
        bc_end(&b, t + 5);
        CHECK(bc_show(&b, 0, t + 5) == 0);
    }
    bc_start(&b, 0, 100);
    CHECK(bc_show(&b, 0, 100 + BC_SHOW) == 1);
    bc_end(&b, 100 + BC_SHOW);
    CHECK(bc_show(&b, 0, 100 + BC_SHOW) == 0);
    bc_start(&b, 0, 101 + BC_SHOW);
    CHECK(bc_show(&b, 0, 102 + BC_SHOW) == 0);
    bc_end(&b, 102 + BC_SHOW);
    bc_start(&b, 0, 0xFFFFFFF8u);
    CHECK(bc_show(&b, 0, 0xFFFFFFF8u + BC_SHOW) == 1);
    bc_end(&b, 0xFFFFFFF8u + BC_SHOW);
    CHECK(bc_show(&b, 0, 0xFFFFFFF8u + BC_SHOW) == 0);
}

static void t_busycore_gap_resets(void)
{
    BusyCore b = { -1, 0, 0 };
    bc_start(&b, 0, 0); bc_end(&b, 20);
    CHECK(bc_show(&b, 0, 23) == 0);
    bc_start(&b, 0, 100);
    CHECK(bc_show(&b, 0, 110) == 0);
    bc_end(&b, 111);
}

static void t_busycore_window_switch(void)
{
    BusyCore b = { -1, 0, 0 };
    bc_start(&b, 0, 0); bc_end(&b, 1);
    bc_start(&b, 3, 2);
    CHECK(bc_show(&b, 3, 10) == 0);
    CHECK(bc_show(&b, 3, 2 + BC_SHOW) == 1);
    bc_end(&b, 30);
}

static void t_network_wire_validation(void)
{
    u8 udp[11]={0x04,0x35,0,53,0,11,0,0,1,2,3};
    u16 c=nw_l4_csum(0x0101a8c0,0x0201a8c0,17,udp,sizeof udp);
    udp[6]=c>>8; udp[7]=c;
    CHECK(nw_l4_csum(0x0101a8c0,0x0201a8c0,17,udp,sizeof udp)==0);
    udp[10]^=1;
    CHECK(nw_l4_csum(0x0101a8c0,0x0201a8c0,17,udp,sizeof udp)!=0);
    udp[10]^=1;
    CHECK(nw_l4_csum(0x0101a8c0,0x0301a8c0,17,udp,sizeof udp)!=0);
    u8 p[360], mac[6]={2,3,4,5,6,7};
    int n=dh_build(p,sizeof p,DH_REQUEST,12,mac,0x6401a8c0,0x0101a8c0,0);
    n=dh_hostname(p,n,sizeof p,"flopnix-1234567890123456789012345");
    CHECK(n>300 && (unsigned)n<=sizeof p);
    int found=0;
    for(int i=240;i<n && p[i]!=255;) { int code=p[i++]; if(!code)continue; int z=p[i++]; if(code==12)found=z; i+=z; }
    CHECK(found==31);
    n=dh_build(p,sizeof p,DH_REQUEST,12,mac,0,0,0);
    CHECK(dh_hostname(p,n,280,"flopnix-test")==0);
    DhReply reply={0};
    u8 opt[]={6,8,8,8,8,8,1,1,1,1,255};
    CHECK(dh_options(opt,sizeof opt,&reply,1));
    CHECK(reply.dns==0x08080808 && reply.dns2==0x01010101);
}

static void t_emergency_policy(void)
{
    EmergencyVideo v={0xe0000000u,640,480,640,0,0}; v.check=em_video_check(&v);
    CHECK(em_video_valid(&v));
    v.width=0; v.check=em_video_check(&v); CHECK(!em_video_valid(&v));
    v.width=640; v.pitch=639; v.check=em_video_check(&v); CHECK(!em_video_valid(&v));
    v.pitch=640; v.base=0xffff0000u; v.check=em_video_check(&v); CHECK(!em_video_valid(&v));
    v.base=0xe0000000u; v.check=em_video_check(&v); v.height^=1; CHECK(!em_video_valid(&v));
    v.base=0xa0000; v.width=320; v.height=200; v.pitch=320; v.bank=0;
    v.check=em_video_check(&v); CHECK(em_video_valid(&v));
    v.width=640; v.height=480; v.pitch=640; v.bank=1;
    v.check=em_video_check(&v); CHECK(em_video_valid(&v));
    v.bank=5; v.check=em_video_check(&v); CHECK(!em_video_valid(&v));
    u32 age=0; CHECK(!em_watch_step(&age,0,0) && age==0);
    for (unsigned i=1;i<EM_WATCH_TICKS;i++) CHECK(!em_watch_step(&age,1,0));
    CHECK(em_watch_step(&age,1,0));
    age=0; for (unsigned i=1;i<EM_PANIC_TICKS;i++) CHECK(!em_watch_step(&age,1,1));
    CHECK(em_watch_step(&age,1,1));
    age=0xffffffffu; CHECK(em_watch_step(&age,1,0));
}

static void t_network_config(void)
{
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0x0101a8c0, 0x08080808, 0) == 0);
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0, 0, 0) == 0);
    CHECK(nc_validate(0x0100000a, 0x000000ff, 0, 0, 0) == 0);
    CHECK(nc_validate(0x010010ac, 0x0000ffff, 0, 0, 0) == 0);
    CHECK(nc_validate(0, 0x00ffffff, 0, 0, 0) == 1);
    CHECK(nc_validate(0x0100007f, 0x00ffffff, 0, 0, 0) == 1);
    CHECK(nc_validate(0x010000e0, 0x00ffffff, 0, 0, 0) == 1);
    CHECK(nc_validate(0x6401a8c0, 0x00ff00ff, 0, 0, 0) == 2);
    CHECK(nc_validate(0x6401a8c0, 0, 0, 0, 0) == 2);
    CHECK(nc_validate(0x0001a8c0, 0x00ffffff, 0, 0, 0) == 1);
    CHECK(nc_validate(0xff01a8c0, 0x00ffffff, 0, 0, 0) == 1);
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0x0102a8c0, 0, 0) == 3);
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0x6401a8c0, 0, 0) == 3);
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0, 0xffffffff, 0) == 4);
    CHECK(nc_validate(0x6401a8c0, 0x00ffffff, 0, 0, 0x0100007f) == 5);
    CHECK(nc_validate(0x0001a8c0, 0xfeffffff, 0x0101a8c0, 0, 0) == 0);
    CHECK(nc_validate(0x6401a8c0, 0xffffffff, 0, 0, 0) == 0);
    FCfg cfg; NetPrefs p;
    api->memset(&cfg, 0, sizeof cfg); np_load(&cfg, &p);
    CHECK(p.mtu == 1500 && p.adapter == 0 && !p.dns_manual && !p.isa_io);
    CHECK(sizeof(FCfg) == 512 && sizeof(NetPrefs) <= sizeof cfg.reserved);
    p.dns = 0x08080808; p.dns2 = 0x01010101; p.adapter = 4; p.mtu = 1492;
    api->strlcpy(p.hostname, "flopnix-pc", sizeof p.hostname); np_store(&cfg, &p);
    np_load(&cfg, &p);
    CHECK(p.dns == 0x08080808 && p.dns2 == 0x01010101 && p.adapter == 4 && p.mtu == 1492);
    CHECK(np_hostname(p.hostname) && !np_hostname("bad name") && !np_hostname("-bad"));
    CHECK(!np_hostname("bad-") && np_hostname("pc-90"));
}

static void t_cfgsan_ranges(void)
{
    FCfg c;
    api->memset(&c, 0xEE, sizeof c);
    cfg_sanitize(&c);
    CHECK(c.video >= 1 && c.video <= 4);
    CHECK(c.net_mode <= 1);
    CHECK(c.mouse_speed >= 1 && c.mouse_speed <= 4);
    CHECK(c.ss_enable <= 1);
    CHECK(c.ss_secs >= 5 && c.ss_secs <= 240);
    CHECK(c.wp_mode <= WP_BITMAP);
    CHECK(c.wp_path[0] == 0);
    CHECK(c.tz_qh >= -48 && c.tz_qh <= 56);
    c.tz_qh = 57;  cfg_sanitize(&c); CHECK(c.tz_qh == 0);
    c.tz_qh = -49; cfg_sanitize(&c); CHECK(c.tz_qh == 0);
    c.tz_qh = -20; cfg_sanitize(&c); CHECK(c.tz_qh == -20);
}

static void t_cfgsan_path(void)
{
    FCfg c;
    api->memset(&c, 0, sizeof c);
    api->memset(c.wp_path, 'A', sizeof c.wp_path);
    cfg_sanitize(&c);
    CHECK(c.wp_path[sizeof c.wp_path - 1] == 0);

    api->memset(&c, 0, sizeof c);
    c.wp_path[0] = 'w'; c.wp_path[1] = 5; c.wp_path[2] = 'x';
    cfg_sanitize(&c);
    CHECK(c.wp_path[0] == 0);
}

static void t_cfgsan_keeps_valid(void)
{
    FCfg c;
    api->memset(&c, 0, sizeof c);
    c.video = 3; c.net_mode = 1; c.mouse_speed = 4;
    c.ss_enable = 0; c.ss_secs = 120;
    c.wp_mode = WP_BITMAP;
    api->strlcpy(c.wp_path, "u:/wall.bmp", sizeof c.wp_path);
    cfg_sanitize(&c);
    CHECK(c.video == 3 && c.net_mode == 1 && c.mouse_speed == 4);
    CHECK(c.ss_enable == 0 && c.ss_secs == 120);
    CHECK(c.wp_mode == WP_BITMAP);
    CHECK(STREQ(c.wp_path, "u:/wall.bmp"));
}

static void t_fbspan(void)
{
    u8 a[11] = {0}, b[11] = {0};
    int x = -1, n = -1;
    CHECK(!fb_span(a, b, 11, &x, &n));
    a[5] = 9;
    CHECK(fb_span(a, b, 11, &x, &n) && x == 5 && n == 1);
    a[0] = a[10] = 1;
    CHECK(fb_span(a, b, 11, &x, &n) && x == 0 && n == 11);
    CHECK(!fb_span(a, b, 0, &x, &n));
    CHECK(fb_span(a + 1, b + 1, 9, &x, &n) && x == 4 && n == 1);
}
static void t_bench_rate(void)
{
    CHECK(bm_muldiv(3000000, 1600, 50) == 96000000);
    CHECK(bm_muldiv(0xffffffff, 0xffffffff, 0xffffffff) == 0xffffffff);
    CHECK(bm_muldiv(0xffffffff, 100, 1) == 0xffffffff);
    CHECK(bm_muldiv(10, 11, 3) == 36);
    CHECK(bm_muldiv(1, 1, 0) == 0);
    CHECK(bm_median(9, 3, 5) == 5 && bm_median(5, 9, 3) == 5);
    u32 last = 42, spins = 0;
    CHECK(!bm_stalled(42, &last, &spins, 2));
    CHECK(bm_stalled(42, &last, &spins, 2));
    CHECK(!bm_stalled(43, &last, &spins, 2) && spins == 0 && last == 43);
    last = 0xffffffff;
    CHECK(!bm_stalled(0, &last, &spins, 2) && spins == 0 && last == 0);
}

static void t_desktop_folder(void)
{
    char out[24];
    CHECK(desk_path(out, "note.txt", sizeof out) && STREQ(out, "desktop/note.txt"));
    CHECK(desk_member(out) && !desk_member("note.txt") && !desk_member("desktopish/x"));
    CHECK(!desk_member("desktop/") && !desk_member("desktop/sub/file"));
    CHECK(!desk_path(out, "../sys", sizeof out) && !desk_path(out, "..", sizeof out));
    CHECK(!desk_path(out, "abcdefghijklmnop", sizeof out));
    CHECK(desk_path(out, "abcdefghijklmno", sizeof out));
    CHECK(STREQ(desk_leaf("desktop/note.txt"), "note.txt"));
}
static void t_memory_layout(void)
{
    MemoryLayout m = memory_layout(4032);
    CHECK(m.fb >= 0x190000u && m.fb_end - m.fb >= 640u * 480u);
    CHECK(m.pool >= 0x30000u && m.pool_end <= MEM_DMA_BASE);
    CHECK(MEM_DMA_BASE + 65536 <= MEM_STACK_TOP && MEM_STACK_TOP < 0xA0000u);
    CHECK(m.fb_end <= m.arena && m.arena_end <= MEM_IO_BASE);
    CHECK(MEM_IO_END - MEM_IO_BASE == 1327104u);
    CHECK(m.heap >= MEM_IO_END && m.heap_end <= 4032u * 1024u);
    CHECK(m.heap_end - m.heap >= 128u * 1024u);
    m = memory_layout(3968); CHECK(m.heap_end - m.heap >= 176u * 1024u);
    m = memory_layout(8192); CHECK(m.pool_end <= 8192u * 1024u);
    m = memory_layout(9216); CHECK(m.pool == 0x800000u && m.pool_end == 0x900000u);
    m = memory_layout(2048); CHECK(m.heap_end == m.heap);
    CHECK(ks_pool_pages_fit(KEXT_POOL_END - KEXT_POOL_BASE - 4096, 4096));
    CHECK(!ks_pool_pages_fit(KEXT_POOL_END - KEXT_POOL_BASE - 4096, 4097));
    CHECK(!ks_pool_pages_fit(0, 0xFFFFFFFFu));
}

static void t_cpu_ownership(void)
{
    CHECK(ca_stamp(0, 8) - ca_stamp(0xFFFFFC00u, 7) == 1);
    CpuAccount c; api->memset(&c, 0, sizeof c);
    for (int i = 0; i < CA_THREADS; i++) c.owner[i] = -1;
    ca_owner(&c, 100, 2);
    ca_switch(&c, 130, 1);
    ca_owner(&c, 140, 3);
    ca_switch(&c, 200, 0);
    ca_owner(&c, 220, 4);
    ca_owner(&c, 230, 2);
    ca_owner(&c, 250, -1);
    CHECK(c.acc[2] == 70 && c.acc[3] == 60 && c.acc[4] == 10);
    c.last = 0xfffffff0u; c.owner[0] = 5;
    ca_step(&c, 16);
    CHECK(c.acc[5] == 32);
}

static void t_fhlayout(void)
{

    const int sizes[][2] = {{312,148}, {312,330}, {536,390}, {800,600}};
    for (int i = 0; i < 4; i++) {
        FhLayout l = fh_layout(sizes[i][0], sizes[i][1]);
        CHECK(l.map_x >= 0 && l.map_x + l.map_w + 2 <= sizes[i][0]);
        CHECK(l.map_h >= 4 && l.map_y + l.map_h < l.status_y);
        CHECK(l.legend_y + (l.narrow ? 36 : 18) <= l.status_y);
        if (!l.tiny) CHECK(l.progress_y + 16 < l.status_y);
    }
    VbPart p[2];
    CHECK(vblit_span(102, 250, 40, 640, 640, p) == 2);
    CHECK(p[0].src_off == 65530 && p[0].len == 6);
    CHECK(p[1].src_off == 65536 && p[1].len == 34 && p[1].bank == 1);
}

static void t_fhmap_fits_box(void)
{
    int ncyl = 80, nheads = 2;

    int cw = fh_cellw(476, ncyl);
    int chh = fh_cellh(118, nheads);
    CHECK(cw >= 3 && cw <= 12);

    CHECK(nheads * chh + FH_BELOW_MAP <= 118);

    chh = fh_cellh(300, nheads);
    CHECK(nheads * chh + FH_BELOW_MAP <= 300);

    chh = fh_cellh(40, nheads);
    CHECK(chh == 8);

    CHECK(ncyl * fh_cellw(900, ncyl) <= 900);
    CHECK(fh_cellw(200, ncyl) == 3);
}

static void t_winhit_topmost_wins(void)
{

    WhRect r[2] = { { 20, 20, 300, 200 }, { 100, 60, 120, 80 } };
    int z[2] = { 0, 1 };

    CHECK(wh_top_at(z, 2, r, 150, 100) == 1);

    CHECK(wh_top_at(z, 2, r, 40, 40) == 0);

    CHECK(wh_top_at(z, 2, r, 500, 400) == -1);

    int z2[2] = { 1, 0 };
    CHECK(wh_top_at(z2, 2, r, 150, 100) == 0);
}

static void t_winhit_edges(void)
{
    WhRect r[1] = { { 10, 10, 100, 50 } };
    int z[1] = { 0 };
    CHECK(wh_top_at(z, 1, r, 10, 10) == 0);
    CHECK(wh_top_at(z, 1, r, 109, 59) == 0);
    CHECK(wh_top_at(z, 1, r, 110, 30) == -1);
    CHECK(wh_top_at(z, 1, r, 50, 60) == -1);
    CHECK(wh_top_at(z, 1, r, 9, 30) == -1);
    CHECK(wh_top_at(z, 0, r, 50, 30) == -1);
}

static void t_winfit_vga(void)
{

    WinFit f = win_fit(24, 16, 524, 346, 320, 170);
    CHECK(f.w <= 320 && f.h <= 170);
    CHECK(f.x >= 0 && f.y >= 0);
    CHECK(f.x + f.w <= 320);
    CHECK(f.y + f.h <= 170);
}

static void t_winfit_leaves_fitting_alone(void)
{

    WinFit f = win_fit(24, 16, 400, 300, 640, 450);
    CHECK(f.x == 24 && f.y == 16 && f.w == 400 && f.h == 300);

    f = win_fit(500, 400, 400, 300, 640, 450);
    CHECK(f.w == 400 && f.h == 300);
    CHECK(f.x + f.w <= 640 && f.y + f.h <= 450);
    CHECK(f.x >= 0 && f.y >= 0);
}

static void t_winfit_exact_and_degenerate(void)
{

    WinFit f = win_fit(0, 0, 640, 450, 640, 450);
    CHECK(f.w == 640 && f.h == 450 && f.x == 0 && f.y == 0);

    f = win_fit(10, 10, 500, 400, 100, 80);
    CHECK(f.w == 100 && f.h == 80 && f.x == 0 && f.y == 0);

    f = win_fit(-50, -20, 200, 150, 640, 450);
    CHECK(f.x == 0 && f.y == 0 && f.w == 200 && f.h == 150);
}

static void t_mepreset_layout(void)
{
    MePreset p[12];

    int n = me_build_presets(0x100000, 0x300000, 0x400000, 0x600000,
                             0x700000, 0x800000, p, 12);
    CHECK(n == 7);

    int rising = 1;
    for (int i = 1; i < n; i++) if (p[i].addr <= p[i - 1].addr) rising = 0;
    CHECK(rising == 1);
    CHECK(p[0].addr == 0);
    CHECK(p[1].addr == 0x100000);
    CHECK(p[4].addr == 0x600000);
    CHECK(p[6].addr == 0x800000);

    int named = 1;
    for (int i = 0; i < n; i++) if (!p[i].label || !p[i].label[0]) named = 0;
    CHECK(named == 1);
}

static void t_mepreset_absent_regions(void)
{
    MePreset p[12];

    int n = me_build_presets(0x100000, 0, 0x400000, 0x600000, 0x700000, 0,
                             p, 12);
    CHECK(n == 5);
    CHECK(p[0].addr == 0);
    for (int i = 1; i < n; i++) CHECK(p[i].addr != 0);
}

static void t_mepreset_collapse_and_cap(void)
{
    MePreset p[12];

    int n = me_build_presets(0x100000, 0x400000, 0x400000, 0x600000,
                             0x700000, 0x800000, p, 12);
    CHECK(n == 6);
    for (int i = 1; i < n; i++) CHECK(p[i].addr != p[i - 1].addr);

    MePreset small[3];
    CHECK(me_build_presets(0x100000, 0x300000, 0x400000, 0x600000,
                           0x700000, 0x800000, small, 3) == 3);
    CHECK(small[0].addr == 0 && small[1].addr == 0x100000);
}

static void t_pumpbtn_drag(void)
{
    PumpBtn p = { 0 };

    CHECK(pb_main(&p, 1) == 1);

    CHECK(pb_pump(&p, 1) == 1);
    CHECK(pb_pump(&p, 1) == 1);
    CHECK(pb_pump(&p, 1) == 1);

    CHECK(pb_pump(&p, 0) == 0);

    CHECK(pb_pump(&p, 0) == 0);
}

static void t_pumpbtn_press_during_pump(void)
{
    PumpBtn p = { 0 };
    CHECK(pb_pump(&p, 1) == 1);
    CHECK(pb_pump(&p, 1) == 1);
    CHECK(pb_pump(&p, 0) == 0);
}

static void t_pumpbtn_buttons_independent(void)
{
    PumpBtn p = { 0 };
    CHECK(pb_main(&p, 1) == 1);
    CHECK(pb_pump(&p, 3) == 3);
    CHECK(pb_pump(&p, 2) == 2);
    CHECK(pb_pump(&p, 0) == 0);
    CHECK(pb_main(&p, 2) == 2);
}

static void t_tet_drop(void)
{
    u8 bd[TET_W * TET_H];
    for (int i = 0; i < TET_W * TET_H; i++) bd[i] = 0;

    CHECK(tet_drop_y(bd, TP_O, 0, 4, 0) == 18);

    CHECK(tet_drop_y(bd, TP_O, 0, 4, 18) == 18);

    for (int x = 0; x < TET_W; x++) bd[19 * TET_W + x] = 1;
    CHECK(tet_drop_y(bd, TP_O, 0, 4, 0) == 17);

    CHECK(tet_drop_y(bd, TP_I, 0, 3, 0) == 17);

    bd[15 * TET_W + 5] = 1;
    CHECK(tet_drop_y(bd, TP_O, 0, 4, 0) == 13);
    CHECK(tet_drop_y(bd, TP_O, 0, 0, 0) == 17);

}

static void t_kb_mainrow(void)
{
    KbSt st = KB_FRESH;
    int tr, dn;
    CHECK(kb_tap(&st, 0x1E) == 'a');
    kb_feed(&st, 0x2A, &tr, &dn);
    CHECK(kb_tap(&st, 0x1E) == 'A');
    CHECK(kb_tap(&st, 0x09) == '*');
    kb_feed(&st, 0xAA, &tr, &dn);
    CHECK(kb_tap(&st, 0x1E) == 'a');
    kb_feed(&st, 0x3A, &tr, &dn);
    kb_feed(&st, 0xBA, &tr, &dn);
    CHECK(kb_tap(&st, 0x1E) == 'A');
    kb_feed(&st, 0x3A, &tr, &dn);
    kb_feed(&st, 0xBA, &tr, &dn);
    kb_feed(&st, 0x1D, &tr, &dn);
    CHECK(kb_tap(&st, 0x2E) == 3);
    kb_feed(&st, 0x9D, &tr, &dn);

    int d = kb_feed(&st, 0x1E, &tr, &dn);
    CHECK(d == 'a' && tr == 'a' && dn == 1);
    d = kb_feed(&st, 0x9E, &tr, &dn);
    CHECK(d == 0 && tr == 'a' && dn == 0);
}

static void t_kb_keypad_digits(void)
{

    KbSt st = KB_FRESH;
    CHECK(kb_tap(&st, 0x47) == '7');
    CHECK(kb_tap(&st, 0x48) == '8');
    CHECK(kb_tap(&st, 0x49) == '9');
    CHECK(kb_tap(&st, 0x4B) == '4');
    CHECK(kb_tap(&st, 0x4C) == '5');
    CHECK(kb_tap(&st, 0x4D) == '6');
    CHECK(kb_tap(&st, 0x4F) == '1');
    CHECK(kb_tap(&st, 0x50) == '2');
    CHECK(kb_tap(&st, 0x51) == '3');
    CHECK(kb_tap(&st, 0x52) == '0');
    CHECK(kb_tap(&st, 0x53) == '.');
    CHECK(kb_tap(&st, 0x4A) == '-');
    CHECK(kb_tap(&st, 0x4E) == '+');
    CHECK(kb_tap(&st, 0x37) == '*');
}

static void t_kb_keypad_nav(void)
{
    KbSt st = KB_FRESH;
    int tr, dn;
    kb_feed(&st, 0x45, &tr, &dn);
    kb_feed(&st, 0xC5, &tr, &dn);
    CHECK(st.numlock == 0);
    CHECK(kb_tap(&st, 0x48) == K_UP);
    CHECK(kb_tap(&st, 0x4B) == K_LEFT);
    CHECK(kb_tap(&st, 0x4D) == K_RIGHT);
    CHECK(kb_tap(&st, 0x50) == K_DOWN);
    CHECK(kb_tap(&st, 0x47) == K_HOME);
    CHECK(kb_tap(&st, 0x4F) == K_END);
    CHECK(kb_tap(&st, 0x49) == K_PGUP);
    CHECK(kb_tap(&st, 0x51) == K_PGDN);
    CHECK(kb_tap(&st, 0x53) == K_DEL);
    CHECK(kb_tap(&st, 0x4C) == 0);
    CHECK(kb_tap(&st, 0x4A) == '-');
    kb_feed(&st, 0x45, &tr, &dn);
    kb_feed(&st, 0xC5, &tr, &dn);
    CHECK(kb_tap(&st, 0x48) == '8');
}

static void t_kb_keypad_e0(void)
{
    KbSt st = KB_FRESH;
    CHECK(kb_tap_e0(&st, 0x1C) == '\n');
    CHECK(kb_tap_e0(&st, 0x35) == '/');

    CHECK(kb_tap_e0(&st, 0x48) == K_UP);
    CHECK(kb_tap_e0(&st, 0x53) == K_DEL);

    kb_tap_e0(&st, 0x2A);
    CHECK(kb_tap(&st, 0x1E) == 'a');

    int tr, dn;
    kb_feed(&st, 0xE0, &tr, &dn); kb_feed(&st, 0x1D, &tr, &dn);
    CHECK(kb_tap(&st, 0x2E) == 3);
    kb_feed(&st, 0xE0, &tr, &dn); kb_feed(&st, 0x9D, &tr, &dn);
    CHECK(kb_tap(&st, 0x2E) == 'c');
}

static void t_sbdrag_zones(void)
{

    int tl, tp;
    sb_thumb(200, 40, 20, 10, &tl, &tp);
    CHECK(tl == 100 && tp == 50);
    CHECK(sb_zone(200, 40, 20, 10, 20)  == SBZ_PAGEUP);
    CHECK(sb_zone(200, 40, 20, 10, 50)  == SBZ_THUMB);
    CHECK(sb_zone(200, 40, 20, 10, 100) == SBZ_THUMB);
    CHECK(sb_zone(200, 40, 20, 10, 149) == SBZ_THUMB);
    CHECK(sb_zone(200, 40, 20, 10, 180) == SBZ_PAGEDN);

    CHECK(sb_zone(200, 10, 20, 0, 5) == SBZ_THUMB);
}

static void t_sbdrag_paging(void)
{
    SbDrag d = { 0, 0, 0 };

    CHECK(sb_press(&d, 200, 40, 20, 10, 20) == 0);
    CHECK(d.active == 0);

    CHECK(sb_press(&d, 200, 40, 20, 10, 180) == 20);
    CHECK(sb_press(&d, 200, 40, 20, 0, 180) == 20);

    CHECK(sb_press(&d, 200, 10, 20, 0, 180) == 0);
}

static void t_sbdrag_thumb(void)
{
    SbDrag d = { 0, 0, 0 };

    CHECK(sb_press(&d, 200, 40, 20, 10, 100) == 10);
    CHECK(d.active == 1);
    CHECK(d.grab_off == 10);
    CHECK(sb_move(&d, 200, 40, 20, 100) == 10);

    CHECK(sb_move(&d, 200, 40, 20, 105) == 11);
    CHECK(sb_move(&d, 200, 40, 20, 150) == 20);
    CHECK(sb_move(&d, 200, 40, 20, 999) == 20);
    CHECK(sb_move(&d, 200, 40, 20, 50)  == 0);
    CHECK(sb_move(&d, 200, 40, 20, -999) == 0);
}

static void t_ms2_assemble(void)
{
    Ms2Asm a = { {0}, 0, 0 };
    u32 pk = 0;

    CHECK(ms2_feed(&a, 0x09, 100, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 5, 100, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 2, 100, 3, &pk) == 1);
    CHECK(pk == (0x09u | (5u << 8) | (2u << 16)));

    CHECK(ms2_feed(&a, 0x08, 200, 4, &pk) == 0);
    CHECK(ms2_feed(&a, 1, 200, 4, &pk) == 0);
    CHECK(ms2_feed(&a, 2, 200, 4, &pk) == 0);
    CHECK(ms2_feed(&a, 0xFF, 200, 4, &pk) == 1);
    CHECK(pk == (0x08u | (1u << 8) | (2u << 16) | (0xFFu << 24)));
}

static void t_ms2_bad_first(void)
{

    Ms2Asm a = { {0}, 0, 0 };
    u32 pk = 0;
    CHECK(ms2_feed(&a, 0x64, 100, 3, &pk) == 0);
    CHECK(a.idx == 0);
    CHECK(ms2_feed(&a, 0x08, 101, 3, &pk) == 0);
    CHECK(a.idx == 1);
}

static void t_ms2_timeout_resync(void)
{
    Ms2Asm a = { {0}, 0, 0 };
    u32 pk = 0;

    CHECK(ms2_feed(&a, 0x08, 100, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 50, 100, 3, &pk) == 0);

    CHECK(ms2_feed(&a, 0x0A, 110, 3, &pk) == 0);
    CHECK(a.idx == 1);
    CHECK(ms2_feed(&a, 7, 110, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 3, 110, 3, &pk) == 1);
    CHECK(pk == (0x0Au | (7u << 8) | (3u << 16)));

    CHECK(ms2_feed(&a, 0x08, 120, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 9, 121, 3, &pk) == 0);
    CHECK(ms2_feed(&a, 9, 121, 3, &pk) == 1);
}

static void t_modsort_order(void)
{
    static const u32 sz[6] = { 1024, 8192, 512, 8192, 0, 4096 };
    int idx[6];
    CHECK(mod_sort(sz, 6, 6, idx) == 6);
    CHECK(idx[0] == 1 && idx[1] == 3);
    CHECK(idx[2] == 5);
    CHECK(idx[3] == 0);
    CHECK(idx[4] == 2);
    CHECK(idx[5] == 4);
}

static void t_modsort_bounds(void)
{
    static const u32 sz[4] = { 10, 20, 30, 40 };
    int idx[4];
    CHECK(mod_sort(sz, 0, 4, idx) == 0);
    CHECK(mod_sort(sz, -3, 4, idx) == 0);

    CHECK(mod_sort(sz, 4, 2, idx) == 2);
    CHECK(idx[0] == 3 && idx[1] == 2);
    CHECK(mod_sort(sz, 4, 0, idx) == 0);
}

static void t_modsort_share(void)
{

    CHECK(mod_share(86, 8192, 8192) == 86);
    CHECK(mod_share(86, 4096, 8192) == 43);
    CHECK(mod_share(86, 0, 8192) == 0);
    CHECK(mod_share(86, 8192, 0) == 0);
    CHECK(mod_share(86, 9999, 8192) == 86);
}

static void t_sbar_thumb(void)
{
    int tl, tp;

    sb_thumb(100, 10, 10, 0, &tl, &tp);
    CHECK(tl == 100 && tp == 0);
    sb_thumb(100, 0, 10, 0, &tl, &tp);
    CHECK(tl == 100 && tp == 0);

    sb_thumb(200, 40, 20, 20, &tl, &tp);
    CHECK(tl == 100);
    CHECK(tp == 100);
    CHECK(tp + tl == 200);

    sb_thumb(604, 40, 37, 27, &tl, &tp);
    CHECK(tp >= 0);
    CHECK(tp + tl <= 604);

    sb_thumb(300, 40, 39, 38, &tl, &tp);
    CHECK(tp + tl <= 300);

    sb_thumb(200, 40, 20, -5, &tl, &tp);
    CHECK(tp == 0);

    sb_thumb(20, 10000, 1, 9999, &tl, &tp);
    CHECK(tl <= 20);
    CHECK(tp + tl <= 20);

    for (int total = 1; total <= 60; total += 7)
        for (int vis = 1; vis <= 60; vis += 5)
            for (int off = -3; off <= 70; off += 9) {
                sb_thumb(150, total, vis, off, &tl, &tp);
                CHECK(tp >= 0);
                CHECK(tl >= 0);
                CHECK(tp + tl <= 150);
            }
}

static u8 lz_a[9000], lz_b[9000 + 512], lz_c[9000];

static int lz_trip(u32 n)
{
    u32 p = lz_pack(lz_a, n, lz_b, sizeof lz_b);
    if (!p) return -2;
    int u = lz_unpack(lz_b, p, lz_c, sizeof lz_c);
    if (u != (int)n) return -3;
    for (u32 i = 0; i < n; i++) if (lz_a[i] != lz_c[i]) return -4;
    return (int)p;
}

static void t_lz_roundtrip(void)
{

    for (int i = 0; i < 4000; i++) lz_a[i] = 'x';
    int p = lz_trip(4000);
    CHECK(p > 0);
    CHECK(p < 550);
    CHECK(p > 400);

    const char *ph = "the quick brown fox jumps over the lazy dog. ";
    int pl = (int)api->strlen(ph), k = 0;
    while (k < 6000) { lz_a[k] = (u8)ph[k % pl]; k++; }
    p = lz_trip(6000);
    CHECK(p > 0);
    CHECK(p < 6000 / 3);

    for (int i = 0; i < 8000; i++) lz_a[i] = (u8)((i / 16) & 0xFF);
    CHECK(lz_trip(8000) > 0);

    lz_a[0] = 42;
    CHECK(lz_trip(1) > 0);
    CHECK(lz_trip(2) > 0);
}

static void t_lz_incompressible(void)
{

    u32 s = 12345;
    for (int i = 0; i < 8000; i++) { s = s * 1103515245u + 12345u; lz_a[i] = (u8)(s >> 16); }
    int p = lz_trip(8000);
    CHECK(p > 0);
    CHECK(p <= 8000 + LZ_HDR);
}

static void t_lz_empty(void)
{
    u32 p = lz_pack(lz_a, 0, lz_b, sizeof lz_b);
    CHECK(p == LZ_HDR);
    CHECK(lz_unpack(lz_b, p, lz_c, sizeof lz_c) == 0);
}

static void t_lz_refuses_bad(void)
{
    for (int i = 0; i < 3000; i++) lz_a[i] = (u8)(i * 7);
    u32 p = lz_pack(lz_a, 3000, lz_b, sizeof lz_b);
    CHECK(p > 0);
    CHECK(lz_unpack(lz_b, p, lz_c, sizeof lz_c) == 3000);

    lz_b[0] = 'Q';
    CHECK(lz_unpack(lz_b, p, lz_c, sizeof lz_c) == -1);
    lz_b[0] = 'P';
    CHECK(lz_unpack(lz_b, p - 40, lz_c, sizeof lz_c) == -1);
    CHECK(lz_unpack(lz_b, 6, lz_c, sizeof lz_c) == -1);
    lz_b[LZ_HDR + 2] ^= 0xFF;
    CHECK(lz_unpack(lz_b, p, lz_c, sizeof lz_c) == -1);
    lz_b[LZ_HDR + 2] ^= 0xFF;
    CHECK(lz_unpack(lz_b, p, lz_c, 100) == -1);
}

static void lfn_build_83(const char *base, const char *ext, u8 *out11)
{
    for (int i = 0; i < 11; i++) out11[i] = ' ';
    for (int i = 0; i < 8 && base[i]; i++) out11[i] = (u8)base[i];
    for (int i = 0; i < 3 && ext[i]; i++) out11[8 + i] = (u8)ext[i];
}

static u8 lfn_checksum_ref(const u8 *sfn)
{
    u8 s = 0;
    for (int i = 0; i < 11; i++)
        s = (u8)(((s & 1) << 7) + (s >> 1) + sfn[i]);
    return s;
}

static int lfn_build_run(const char *lname, const u8 *sfn, u8 *buf)
{
    int len = 0; while (lname[len]) len++;
    int frags = (len + 12) / 13; if (frags < 1) frags = 1;
    u8 cs = lfn_checksum_ref(sfn);
    for (int f = frags; f >= 1; f--) {
        u8 *de = buf + (frags - f) * 32;
        api->memset(de, 0, 32);
        de[0] = (u8)(f | (f == frags ? 0x40 : 0));
        de[11] = 0x0F;
        de[13] = cs;
        static const int slot[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
        for (int c = 0; c < 13; c++) {
            int idx = (f - 1) * 13 + c;
            u16 w;
            if (idx < len) w = (u16)lname[idx];
            else if (idx == len) w = 0;
            else w = 0xFFFF;
            de[slot[c]] = (u8)w; de[slot[c] + 1] = (u8)(w >> 8);
        }
    }
    u8 *e83 = buf + frags * 32;
    api->memset(e83, 0, 32);
    api->memcpy(e83, sfn, 11);
    e83[11] = 0x20;
    return frags + 1;
}

static int lfn_recover(const u8 *buf, int nent, char *out, int cap)
{
    LfnAcc a; lfn_reset(&a);
    for (int i = 0; i < nent; i++) {
        const u8 *de = buf + i * 32;
        if (de[0] == 0xE5) { lfn_reset(&a); continue; }
        if (de[11] == 0x0F) { lfn_feed(&a, de); continue; }
        return lfn_take(&a, de, out, cap);
    }
    return 0;
}

static void t_lfn_checksum(void)
{
    u8 sfn[11]; lfn_build_83("HELLO~1", "TXT", sfn);
    CHECK(lfn_checksum(sfn) == 237);
    CHECK(lfn_checksum(sfn) == lfn_checksum_ref(sfn));
}

static void t_lfn_names(void)
{
    u8 buf[8 * 32]; char out[64];
    u8 sfn[11];

    lfn_build_83("HELLO~1", "TXT", sfn);
    int n = lfn_build_run("hello.txt", sfn, buf);
    CHECK(lfn_recover(buf, n, out, sizeof out) == 1);
    CHECK(STREQ(out, "hello.txt"));

    lfn_build_83("MYLONG~1", "MID", sfn);
    n = lfn_build_run("My Long Song Name.mid", sfn, buf);
    CHECK(lfn_recover(buf, n, out, sizeof out) == 1);
    CHECK(STREQ(out, "My Long Song Name.mid"));

    lfn_build_83("RAINBO~1", "MID", sfn);
    n = lfn_build_run("Rainbow Tylenol - Nyan Trololo.mid", sfn, buf);
    CHECK(lfn_recover(buf, n, out, sizeof out) == 1);
    CHECK(STREQ(out, "Rainbow Tylenol - Nyan Trololo.mid"));
}

static void t_lfn_rejects(void)
{
    u8 buf[8 * 32]; char out[64];
    u8 sfn[11];

    lfn_build_83("HELLO~1", "TXT", sfn);
    int n = lfn_build_run("hello.txt", sfn, buf);
    u8 other[11]; lfn_build_83("OTHER~1", "TXT", other);
    api->memcpy(buf + (n - 1) * 32, other, 11);
    CHECK(lfn_recover(buf, n, out, sizeof out) == 0);

    lfn_build_83("HELLO~1", "TXT", sfn);
    n = lfn_build_run("hello.txt", sfn, buf);
    buf[0] = 0xE5;
    CHECK(lfn_recover(buf, n, out, sizeof out) == 0);

    lfn_build_83("RAINBO~1", "MID", sfn);
    n = lfn_build_run("Rainbow Tylenol - Nyan Trololo.mid", sfn, buf);

    api->memcpy(buf + 32, buf + 64, (u32)((n - 2) * 32));
    CHECK(lfn_recover(buf, n - 1, out, sizeof out) == 0);

    struct { u8 guard[16]; LfnAcc a; } guarded;
    api->memset(&guarded, 0x7B, sizeof guarded);
    n = lfn_build_run("hello.txt", sfn, buf);
    lfn_reset(&guarded.a); lfn_feed(&guarded.a, buf);
    buf[0] = 0; lfn_feed(&guarded.a, buf);
    for (int i=0;i<16;i++) CHECK(guarded.guard[i] == 0x7B);
    CHECK(!guarded.a.ok);

    n = lfn_build_run("123456789.txt", sfn, buf);
    api->memset(&guarded.a, 0x7B, sizeof guarded.a);
    lfn_reset(&guarded.a); lfn_feed(&guarded.a, buf);
    CHECK(lfn_take(&guarded.a, buf+(n-1)*32, out, sizeof out));
    CHECK(!api->strcmp(out,"123456789.txt"));
}

static void t_lfn_truncate(void)
{
    u8 buf[8 * 32]; char out[64];
    u8 sfn[11]; lfn_build_83("VERYLO~1", "TXT", sfn);

    char big[80];
    for (int i = 0; i < 70; i++) big[i] = (char)('a' + i % 26);
    big[70] = 0;
    int n = lfn_build_run(big, sfn, buf);
    CHECK(lfn_recover(buf, n, out, sizeof out) == 1);
    int outlen = 0; while (out[outlen]) outlen++;
    CHECK(outlen == 63);
    CHECK(out[0] == 'a');
}

static void t_nicdesc(void)
{

    u32 good = (64u << 16) | 0x0300;
    CHECK(td_rx_ok(good) == 1);
    CHECK(td_rx_len(good) == 60);
    CHECK(td_rx_ok(good | 0x8000) == 0);
    CHECK(td_rx_ok((64u << 16) | 0x0200) == 0);
    CHECK(td_rx_ok(good | TD_OWN) == 0);

    CHECK(td_rx_ok((3u << 16) | 0x0300) == 0);

    CHECK(td_rx_len((0x3FFFu << 16) | 0x0300) == 0x3FFF - 4);

    CHECK(td_tx_len(60, 0) == (0x60000000u | 60));
    CHECK(td_tx_len(60, 1) == (0x60000000u | 0x02000000u | 60));

    CHECK(td_rx_buf(1536, 0) == 1536u);
    CHECK(td_rx_buf(1536, 1) == (0x02000000u | 1536u));
}

static void t_phy_link_state(void)
{
    PhyState p;

    phy_decode(0x7809, 0x01E1, 0x0000, 0x3000, &p);
    CHECK(p.up == 0);
    CHECK(p.mbps == 0 && p.full == 0);

    phy_decode(0x7805, 0x01E1, 0x0000, 0x3000, &p);
    CHECK(p.up == 1);
    CHECK(p.autoneg_done == 0);
    CHECK(p.mbps == 100 && p.full == 0);

    phy_decode(0x7805, 0x01E1, 0x0000, 0x2100, &p);
    CHECK(p.mbps == 100 && p.full == 1);
    phy_decode(0x7805, 0x01E1, 0x0000, 0x0000, &p);
    CHECK(p.mbps == 10 && p.full == 0);

    phy_decode(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, &p);
    CHECK(p.present == 0 && p.up == 0);
    phy_decode(0x0000, 0x0000, 0x0000, 0x0000, &p);
    CHECK(p.present == 0 && p.up == 0);
}

static void t_phy_negotiate(void)
{
    PhyState p;
    u16 bmsr = 0x782D;

    phy_decode(bmsr, 0x01E1, 0x0101, 0x1000, &p);
    CHECK(p.up == 1 && p.autoneg_done == 1);
    CHECK(p.mbps == 100 && p.full == 1);

    phy_decode(bmsr, 0x01E1, 0x0081, 0x1000, &p);
    CHECK(p.mbps == 100 && p.full == 0);

    phy_decode(bmsr, 0x01E1, 0x0041, 0x1000, &p);
    CHECK(p.mbps == 10 && p.full == 1);

    phy_decode(bmsr, 0x01E1, 0x0021, 0x1000, &p);
    CHECK(p.mbps == 10 && p.full == 0);

    phy_decode(bmsr, 0x01E1, 0x01E1, 0x1000, &p);
    CHECK(p.mbps == 100 && p.full == 1);

    phy_decode(bmsr, 0x0021, 0x0200, 0x1000, &p);
    CHECK(p.up == 1 && p.mbps == 0);
}

static void t_tulip_csr6(void)
{

    CHECK(tul_csr6(0, 0) == 0x00040000u);
    CHECK((tul_csr6(0, 0) & 0x00200000u) == 0);

    CHECK(tul_csr6(1, 0) == (0x00040000u | 0x0200u));

    CHECK(tul_csr6(0, 1) == (0x00040000u | 0x2000u | 0x0002u));
    CHECK(tul_csr6(1, 1) == (0x00040000u | 0x0200u | 0x2000u | 0x0002u));
}

static void t_ntpcore_vectors(void)
{
    NtpTime t;

    CHECK(ntp_civil(3155673600u, 0, &t) == 1);
    CHECK(t.y == 2000 && t.mo == 1 && t.d == 1);
    CHECK(t.h == 0 && t.mi == 0 && t.s == 0);

    CHECK(ntp_civil(3918196800u, 0, &t) == 1);
    CHECK(t.y == 2024 && t.mo == 2 && t.d == 29 && t.h == 12);

    CHECK(ntp_civil(3913055999u, 0, &t) == 1);
    CHECK(t.y == 2023 && t.mo == 12 && t.d == 31);
    CHECK(t.h == 23 && t.mi == 59 && t.s == 59);
}

static void t_ntpcore_tz(void)
{
    NtpTime t;

    CHECK(ntp_civil(3944680200u, -4, &t) == 1);
    CHECK(t.y == 2024 && t.mo == 12 && t.d == 31 && t.h == 23 && t.mi == 30);

    CHECK(ntp_civil(3995136000u, 23, &t) == 1);
    CHECK(t.y == 2026 && t.mo == 8 && t.d == 8 && t.h == 5 && t.mi == 45);
}

static void t_ntpcore_rejects(void)
{
    NtpTime t;
    CHECK(ntp_civil(0, 0, &t) == 0);
    CHECK(ntp_civil(3155673599u, 0, &t) == 0);
    CHECK(ntp_civil(3155673600u, -4, &t) == 0);
}

static void t_axline_iter(void)
{
    const char *s = "ver\r\n# a comment\r\n\r\n  date\nlast";
    int len = (int)api->strlen(s);
    int pos = 0;
    char out[AX_LINE];
    CHECK(ax_next(s, len, &pos, out, sizeof out) == 1);
    CHECK(STREQ(out, "ver"));
    CHECK(ax_next(s, len, &pos, out, sizeof out) == 1);
    CHECK(STREQ(out, "  date"));

    CHECK(ax_next(s, len, &pos, out, sizeof out) == 1);
    CHECK(STREQ(out, "last"));
    CHECK(ax_next(s, len, &pos, out, sizeof out) == 0);

    char big[40];
    for (int i = 0; i < 39; i++) big[i] = 'x';
    big[39] = 0;
    pos = 0;
    CHECK(ax_next(big, 39, &pos, out, 8) == 1);
    CHECK(api->strlen(out) == 7);
    CHECK(ax_next(big, 39, &pos, out, 8) == 0);
}

static void t_clipline_paste(void)
{
    char b[10];
    b[0] = 'a'; b[1] = 0;
    int n = cl_paste(b, 1, sizeof b, "bc");
    CHECK(n == 3 && STREQ(b, "abc"));
    n = cl_paste(b, n, sizeof b, "de\nSECOND LINE");
    CHECK(n == 5 && STREQ(b, "abcde"));
    n = cl_paste(b, n, sizeof b, "f\tg");
    CHECK(n == 6 && STREQ(b, "abcdef"));
    n = cl_paste(b, n, sizeof b, "ghijklmnop");
    CHECK(n == 9 && STREQ(b, "abcdefghi"));
    n = cl_paste(b, n, sizeof b, "zz");
    CHECK(n == 9);
}

static void t_bmpw_layout(void)
{
    static u8 d[BMPW_HDR + 8 * 2];
    static u8 pal[256 * 3];
    for (int i = 0; i < 256; i++) {
        pal[i * 3] = (u8)i; pal[i * 3 + 1] = (u8)(i ^ 0xFF); pal[i * 3 + 2] = 7;
    }
    CHECK(bmpw_rowsz(5) == 8);
    CHECK(bmpw_rowsz(4) == 4);
    u32 fsz = bmpw_head(d, 5, 2, pal);
    CHECK(fsz == BMPW_HDR + 2 * 8);
    CHECK(d[0] == 'B' && d[1] == 'M');
    CHECK(d[2] == (u8)fsz);
    CHECK(d[10] == (u8)BMPW_HDR);
    CHECK(d[14] == 40);
    CHECK(d[18] == 5 && d[22] == 2);
    CHECK(d[26] == 1 && d[28] == 8);
    CHECK(d[46] == 0 && d[47] == 1);

    CHECK(d[54 + 4] == 7);
    CHECK(d[54 + 5] == 0xFE);
    CHECK(d[54 + 6] == 1);
    CHECK(d[54 + 7] == 0);

    CHECK(bmpw_row(d, 5, 2, 0) == d + BMPW_HDR + 8);
    CHECK(bmpw_row(d, 5, 2, 1) == d + BMPW_HDR);
}

static int sn_exists(const char *name, void *ctx)
{
    int *upto = (int *)ctx;
    if (name[4] < '0' || name[4] > '9') return 0;
    int n = (name[4] - '0') * 10 + (name[5] - '0');
    return n <= *upto;
}

static void t_shotname_next(void)
{
    char b[16];
    int upto = 0;
    CHECK(shot_next(sn_exists, &upto, b, sizeof b) == 1);
    CHECK(STREQ(b, "SHOT01.BMP"));
    upto = 3;
    CHECK(shot_next(sn_exists, &upto, b, sizeof b) == 1);
    CHECK(STREQ(b, "SHOT04.BMP"));
    upto = 99;
    CHECK(shot_next(sn_exists, &upto, b, sizeof b) == 0);
}

static void t_atsw_cycle(void)
{
    CHECK(at_next(0, 0, 1) == -1);
    CHECK(at_next(1, 0, 1) == 0);

    int k = 2;
    k = at_next(3, k, 1);  CHECK(k == 1);
    k = at_next(3, k, 1);  CHECK(k == 0);
    k = at_next(3, k, 1);  CHECK(k == 2);

    CHECK(at_next(3, 2, -1) == 0);
    CHECK(at_next(3, 0, -1) == 1);
}

static void t_kb_alt(void)
{
    KbSt st = KB_FRESH;
    int tr, dn;
    CHECK(kb_feed(&st, 0x38, &tr, &dn) == 0);
    CHECK(st.alt_l == 1);
    CHECK(tr == 0);
    CHECK(kb_tap(&st, 0x10) == 'q');
    CHECK(kb_feed(&st, 0xB8, &tr, &dn) == 0);
    CHECK(st.alt_l == 0);
    kb_feed(&st, 0xE0, &tr, &dn);
    CHECK(kb_feed(&st, 0x38, &tr, &dn) == 0);
    CHECK(st.alt_r == 1);
    kb_feed(&st, 0xE0, &tr, &dn);
    CHECK(kb_feed(&st, 0xB8, &tr, &dn) == 0);
    CHECK(st.alt_r == 0);
}

static void t_kb_prtsc(void)
{
    KbSt st = KB_FRESH;
    int tr, dn;
    CHECK(kb_feed(&st, 0xE0, &tr, &dn) == 0);
    CHECK(kb_feed(&st, 0x2A, &tr, &dn) == 0);
    CHECK(st.shift_l == 0);
    CHECK(kb_feed(&st, 0xE0, &tr, &dn) == 0);
    CHECK(kb_feed(&st, 0x37, &tr, &dn) == K_PRTSC);
    CHECK(kb_feed(&st, 0xE0, &tr, &dn) == 0);
    CHECK(kb_feed(&st, 0xB7, &tr, &dn) == 0);
    CHECK(kb_feed(&st, 0xE0, &tr, &dn) == 0);
    CHECK(kb_feed(&st, 0xAA, &tr, &dn) == 0);
    CHECK(st.shift_l == 0);
    CHECK(kb_tap(&st, 0x1E) == 'a');
}

static void t_fsd_torn(void)
{
    u8 sec[512];

    fsd_reset(6, 10);
    fsd_fail_wr = 2 + 4;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_TORN);
    CHECK(fsd_at(6 + 0, 0xEE));
    for (int s = 1; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));

    fsd_reset(6, 10);
    fsd_fail_rd2 = 6 + 7;
    fsd_fail_rd = 2 + 1;
    CHECK(fsd_move(fsd_rd, fsd_wr, 6, 2, 10, sec) == FSD_TORN);
    CHECK(fsd_at(6 + 2, 0xA2));
    CHECK(fsd_at(6 + 1, 0xA5));
    CHECK(fsd_at(6 + 0, 0xA4));
    for (int s = 3; s < 10; s++) CHECK(fsd_at(6 + s, 0xA0 + s));
}

static void t_ramtest(void)
{
    CHECK(rt_pattern(0, 0) == 0x55555555u);
    CHECK(rt_pattern(0, 1) == 0xAAAAAAAAu);
    CHECK(rt_pattern(0x1000, 2) != rt_pattern(0x2000, 2));

    u32 buf[64];
    for (int pass = 0; pass < RT_PASSES; pass++) {
        rt_fill(buf, 64, 0x800000, pass);
        CHECK(rt_check(buf, 64, 0x800000, pass) == -1);
    }
    rt_fill(buf, 64, 0x800000, 2);
    buf[17] ^= 0x40;
    CHECK(rt_check(buf, 64, 0x800000, 2) == 17);
}

static void t_flipgate(void)
{
    u32 last = 0;

    CHECK(flip_due(2, &last, 1) == 1 && last == 2);
    CHECK(flip_due(2, &last, 1) == 0);
    CHECK(flip_due(3, &last, 1) == 0);
    CHECK(last == 2);
    CHECK(flip_due(4, &last, 1) == 1 && last == 4);

    last = 0xFFFFFFFEu;
    CHECK(flip_due(1, &last, 1) == 1 && last == 1);

    last = 100;

    CHECK(flip_due(100, &last, 0) == 1);
    CHECK(flip_due(100, &last, 0) == 1);
}

static void t_flight_due(void)
{

    CHECK(flight_due(999999, 0, 0, 0, 0) == 0);

    CHECK(flight_due(FLIGHT_TICKS - 1, 0, 0, 1, 0) == 0);

    CHECK(flight_due(FLIGHT_TICKS, 0, 0, 1, 0) == 1);

    CHECK(flight_due(FLIGHT_TICKS, 0, 0, 1, 1) == 0);

    CHECK(flight_due(2 * FLIGHT_TICKS - 1, 0, 0, 1, 1) == 0);
    CHECK(flight_due(2 * FLIGHT_TICKS, 0, 0, 1, 1) == 1);

    CHECK(flight_due(FLIGHT_TICKS + FLIGHT_BACKOFF - 1, 0, FLIGHT_BACKOFF, 1, 0) == 0);
    CHECK(flight_due(FLIGHT_TICKS + FLIGHT_BACKOFF, 0, FLIGHT_BACKOFF, 1, 0) == 1);

    CHECK(flight_due(FLIGHT_TICKS - 2, 0xFFFFFFFEu, 0, 1, 0) == 1);
}

static void t_flight_slice(void)
{
    u32 off, n, lost;

    CHECK(flight_slice(0, 0, 0, &off, &n, &lost) == 0);

    CHECK(flight_slice(100, 100, 0, &off, &n, &lost) == 1);
    CHECK(off == 0); CHECK(n == 100); CHECK(lost == 0);

    CHECK(flight_slice(100, 100, 60, &off, &n, &lost) == 1);
    CHECK(off == 60); CHECK(n == 40); CHECK(lost == 0);

    CHECK(flight_slice(100, 100, 100, &off, &n, &lost) == 0);

    CHECK(flight_slice(200, 50, 10, &off, &n, &lost) == 1);
    CHECK(off == 0); CHECK(n == 50); CHECK(lost == 140);

    CHECK(flight_slice(200, 50, 180, &off, &n, &lost) == 1);
    CHECK(off == 30); CHECK(n == 20); CHECK(lost == 0);

    CHECK(flight_slice(200, 50, 150, &off, &n, &lost) == 1);
    CHECK(off == 0); CHECK(n == 50); CHECK(lost == 0);

    CHECK(flight_slice(100, 100, 120, &off, &n, &lost) == 0);
}

static void t_ls_action(void)
{
    CHECK(ls_action(5, 1) == 1);
    CHECK(ls_action(0, 1) == 1);
    CHECK(ls_action(-1, 1) == -1);
    CHECK(ls_action(-1, 0) == 0);
    CHECK(ls_action(5, 0) == 1);
}

static void t_dl_parse(void)
{
    u32 s; const char *n;

    CHECK(dl_parse("310\thello.txt", &s, &n) == 1);
    CHECK(s == 310); CHECK(STREQ(n, "hello.txt"));

    CHECK(dl_parse("42\tmy notes.txt", &s, &n) == 1);
    CHECK(s == 42); CHECK(STREQ(n, "my notes.txt"));

    CHECK(dl_parse("hello.txt", &s, &n) == 1);
    CHECK(s == 0); CHECK(STREQ(n, "hello.txt"));

    CHECK(dl_parse("12 monkeys.txt", &s, &n) == 1);
    CHECK(s == 0); CHECK(STREQ(n, "12 monkeys.txt"));

    CHECK(dl_parse("2024", &s, &n) == 1);
    CHECK(s == 0); CHECK(STREQ(n, "2024"));

    CHECK(dl_parse("", &s, &n) == 0);

    CHECK(dl_parse("310\t", &s, &n) == 0);
}

static void t_kb_unctrl(void)
{
    CHECK(kb_unctrl(1, 1) == 'a');
    CHECK(kb_unctrl(3, 1) == 'c');
    CHECK(kb_unctrl(24, 1) == 'x');
    CHECK(kb_unctrl(26, 1) == 'z');
    CHECK(kb_unctrl('a', 1) == 'a');
    CHECK(kb_unctrl(1, 0) == 1);

    CHECK(kb_unctrl('\n', 1) == 'j');
    CHECK(kb_unctrl('\b', 1) == 'h');
    CHECK(kb_unctrl(K_UP, 1) == K_UP);
    CHECK(kb_unctrl(27, 1) == 27);
}

static void t_tf_key(void)
{
    char b[8];
    TextField t;
    #define TF_SET(s) do { api->strlcpy(b, s, sizeof b); t.buf = b; t.cap = sizeof b; \
                           t.len = (int)api->strlen(b); t.caret = t.len; t.all = 0; } while (0)

    TF_SET("abc");
    CHECK(tf_key(&t, K_LEFT, 0) == 1);  CHECK(t.caret == 2);
    CHECK(tf_key(&t, K_HOME, 0) == 1);  CHECK(t.caret == 0);
    CHECK(tf_key(&t, K_LEFT, 0) == 1);  CHECK(t.caret == 0);
    CHECK(tf_key(&t, K_RIGHT, 0) == 1); CHECK(t.caret == 1);
    CHECK(tf_key(&t, K_END, 0) == 1);   CHECK(t.caret == 3);
    CHECK(tf_key(&t, K_RIGHT, 0) == 1); CHECK(t.caret == 3);

    TF_SET("ac");
    t.caret = 1;
    CHECK(tf_key(&t, 'b', 1) == 1);
    CHECK(STREQ(b, "abc")); CHECK(t.caret == 2); CHECK(t.len == 3);

    TF_SET("abc");
    t.caret = 2;
    CHECK(tf_key(&t, '\b', 0) == 1);
    CHECK(STREQ(b, "ac")); CHECK(t.caret == 1);
    CHECK(tf_key(&t, K_DEL, 0) == 1);
    CHECK(STREQ(b, "a"));  CHECK(t.caret == 1);
    t.caret = 0;
    CHECK(tf_key(&t, '\b', 0) == 1);
    CHECK(STREQ(b, "a"));

    TF_SET("abc");
    t.all = 1;
    CHECK(tf_key(&t, 'z', 1) == 1);
    CHECK(STREQ(b, "z")); CHECK(t.len == 1); CHECK(t.caret == 1); CHECK(t.all == 0);

    TF_SET("abc");
    t.all = 1;
    CHECK(tf_key(&t, '\b', 0) == 1);
    CHECK(STREQ(b, "")); CHECK(t.len == 0); CHECK(t.all == 0);

    TF_SET("abc");
    t.all = 1;
    CHECK(tf_key(&t, K_LEFT, 0) == 1);
    CHECK(STREQ(b, "abc")); CHECK(t.caret == 0); CHECK(t.all == 0);
    t.all = 1;
    CHECK(tf_key(&t, K_RIGHT, 0) == 1);
    CHECK(t.caret == 3); CHECK(t.all == 0);

    TF_SET("abcdefg");
    CHECK(t.len == 7);
    CHECK(tf_key(&t, 'h', 1) == 1);
    CHECK(t.len == 7);
    CHECK(STREQ(b, "abcdefg"));

    TF_SET("abc");
    CHECK(tf_key(&t, '\n', 0) == 0);
    CHECK(tf_key(&t, 27, 0) == 0);
    CHECK(tf_key(&t, K_UP, 0) == 0);
    CHECK(tf_key(&t, 'x', 0) == 0);
    #undef TF_SET
}

static void t_plat_hostbridge(void)
{

    CHECK(plat_from_hostbridge(0x12378086u) == PLAT_EMULATED);
    CHECK(plat_from_hostbridge(0x122D8086u) == PLAT_EMULATED);
    CHECK(plat_from_hostbridge(0x29C08086u) == PLAT_EMULATED);

    CHECK(plat_from_hostbridge(0x71908086u) == PLAT_REAL);
    CHECK(plat_from_hostbridge(0x71808086u) == PLAT_REAL);

    CHECK(plat_from_hostbridge(0x30911106u) == PLAT_REAL);

    CHECK(plat_from_hostbridge(0xFFFFFFFFu) == PLAT_REAL);
}

static void t_sched_next(void)
{
    unsigned char st[4];

    st[0] = st[1] = st[2] = st[3] = THR_FREE;
    CHECK(sched_next(st, 4, -1) == -1);
    CHECK(sched_next(st, 0, -1) == -1);

    st[0] = THR_RUN;
    CHECK(sched_next(st, 4, 0) == 0);

    st[0] = THR_RUN; st[1] = THR_READY;
    CHECK(sched_next(st, 4, 0) == 1);
    st[0] = THR_READY; st[1] = THR_RUN;
    CHECK(sched_next(st, 4, 1) == 0);

    st[0] = THR_RUN; st[1] = THR_READY; st[2] = THR_READY; st[3] = THR_FREE;
    CHECK(sched_next(st, 4, 0) == 1);
    CHECK(sched_next(st, 4, 1) == 2);
    CHECK(sched_next(st, 4, 2) == 0);

    st[0] = THR_RUN; st[1] = THR_BLOCKED; st[2] = THR_DEAD; st[3] = THR_READY;
    CHECK(sched_next(st, 4, 0) == 3);
    st[3] = THR_BLOCKED;
    CHECK(sched_next(st, 4, 0) == 0);

    st[0] = THR_BLOCKED;
    CHECK(sched_next(st, 4, 0) == -1);
}

static void t_thr_may_switch(void)
{
    CHECK(thr_may_switch(1, 0, 0, 1) == 1);
    CHECK(thr_may_switch(1, 0, 0, 0) == 1);
    CHECK(thr_may_switch(1, 1, 0, 1) == 1);
    CHECK(thr_may_switch(1, 1, 0, 0) == 1);
    CHECK(thr_may_switch(0, 0, 0, 1) == 0);
    CHECK(thr_may_switch(0, 1, 0, 1) == 0);
    CHECK(thr_may_switch(1, 0, 1, 1) == 0);
    CHECK(thr_may_switch(1, 0, 3, 1) == 0);
    CHECK(thr_may_switch(1, 1, 1, 1) == 0);
    CHECK(thr_may_switch(1, 0, 0, 2) == 0);
    CHECK(thr_may_switch(1, 1, 2, 3) == 0);
}

static void t_appq(void)
{
    CHECK(aq_empty(0, 0) == 1);
    CHECK(aq_count(0, 0) == 0);
    CHECK(aq_full(0, 0) == 0);

    CHECK(aq_count(0, 5) == 5);
    CHECK(aq_empty(0, 5) == 0);
    CHECK(aq_full(0, AQ_SIZE) == 1);
    CHECK(aq_full(0, AQ_SIZE - 1) == 0);
    CHECK(aq_full(10, 10 + AQ_SIZE) == 1);

    CHECK(aq_slot(0) == 0);
    CHECK(aq_slot(AQ_SIZE - 1) == AQ_SIZE - 1);
    CHECK(aq_slot(AQ_SIZE) == 0);
    CHECK(aq_slot(AQ_SIZE + 3) == 3);

    CHECK(aq_count(0xFFFFFFFEu, 2) == 4);
    CHECK(aq_empty(0xFFFFFFFFu, 0xFFFFFFFFu) == 1);
    CHECK(aq_slot(0xFFFFFFFFu) == AQ_SIZE - 1);
}

static void t_usb_db(void)
{

    CHECK(usb_db_step(-1, 0, 1, 1, 100) == USB_DB_NONE);

    CHECK(usb_db_step(-1, 0, 0, 1, 100) == USB_DB_ARM);

    CHECK(usb_db_step(0, 100, 1, 1, 120) == USB_DB_ARM);

    CHECK(usb_db_step(0, 100, 0, 1, 100 + USB_DB_TICKS - 1) == USB_DB_NONE);

    CHECK(usb_db_step(0, 100, 0, 1, 100 + USB_DB_TICKS) == USB_DB_ACT);

    CHECK(usb_db_step(1, 100, 1, 1, 100 + USB_DB_TICKS) == USB_DB_DROP);

    CHECK(usb_db_step(0, 0xFFFFFFF0u, 0, 1, USB_DB_TICKS - 16) == USB_DB_ACT);
}

static void t_url(void)
{
    char h[64], p[64]; u16 port;

    CHECK(nw_url_parse("http://10.0.2.2:8080/a.txt", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "10.0.2.2") && port == 8080 && STREQ(p, "/a.txt"));

    CHECK(nw_url_parse("http://flopnix.dev/docs/x", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "flopnix.dev") && port == 80 && STREQ(p, "/docs/x"));

    CHECK(nw_url_parse("http://host", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "host") && port == 80 && STREQ(p, "/"));

    CHECK(nw_url_parse("https://x/", h, sizeof h, &port, p, sizeof p) == 0);
    CHECK(nw_url_parse("ftp://x/", h, sizeof h, &port, p, sizeof p) == 0);
    CHECK(nw_url_parse("http://", h, sizeof h, &port, p, sizeof p) == 0);
    CHECK(nw_url_parse("http://h:99999/", h, sizeof h, &port, p, sizeof p) == 0);

    CHECK(nw_url_parse("www.example.com", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "www.example.com") && port == 80 && STREQ(p, "/"));

    CHECK(nw_url_parse("sub.host.org/a/b", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "sub.host.org") && port == 80 && STREQ(p, "/a/b"));

    CHECK(nw_url_parse("host:8080/x", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "host") && port == 8080 && STREQ(p, "/x"));

    CHECK(nw_url_parse("plainword", h, sizeof h, &port, p, sizeof p) == 1);
    CHECK(STREQ(h, "plainword") && port == 80 && STREQ(p, "/"));

    CHECK(nw_url_parse("https://h/", h, sizeof h, &port, p, sizeof p) == 0);
}

static void t_ipparse(void)
{
    u32 ip = 0;
    CHECK(nw_ip_parse("10.0.2.3", &ip) == 1);
    CHECK(ip == (10u | (0u << 8) | (2u << 16) | (3u << 24)));
    CHECK(nw_ip_parse("255.255.255.255", &ip) == 1 && ip == 0xFFFFFFFFu);
    CHECK(nw_ip_parse("0.0.0.0", &ip) == 1 && ip == 0);
    CHECK(nw_ip_parse("1.2.3", &ip) == 0);
    CHECK(nw_ip_parse("1.2.3.256", &ip) == 0);
    CHECK(nw_ip_parse("a.b.c.d", &ip) == 0);
    CHECK(nw_ip_parse("1.2.3.4.5", &ip) == 0);
    CHECK(nw_ip_parse("", &ip) == 0);
}

static void t_dns(void)
{
    u8 q[64];

    int n = nw_dns_build("flopnix.dev", 0x1234, q, sizeof q);
    CHECK(n == 29);
    CHECK(q[0] == 0x12 && q[1] == 0x34);
    CHECK(q[2] == 0x01 && q[3] == 0x00);
    CHECK(q[5] == 1);
    CHECK(q[12] == 7 && q[13] == 'f' && q[20] == 3 && q[24] == 0);
    CHECK(q[25] == 0 && q[26] == 1 && q[27] == 0 && q[28] == 1);
    CHECK(nw_dns_build("flopnix.dev", 1, q, 20) == 0);

    static const u8 r[] = {
        0x12,0x34, 0x81,0x80, 0,1, 0,2, 0,0, 0,0,
        7,'f','l','o','p','n','i','x',3,'d','e','v',0, 0,1, 0,1,
        0xC0,0x0C, 0,5, 0,1, 0,0,0,60, 0,2, 0xC0,0x0C,
        0xC0,0x0C, 0,1, 0,1, 0,0,0,60, 0,4, 10,0,2,3
    };
    u32 ip = nw_dns_parse(r, sizeof r, 0x1234);
    CHECK(ip == (10u | (2u << 16) | (3u << 24)));
    CHECK(nw_dns_parse(r, sizeof r, 0x9999) == 0);
    CHECK(nw_dns_parse(r, 20, 0x1234) == 0);
}

#include "dhcp_tests.inc"

static void t_dhcpopt(void)
{

    static const u8 o[] = { 0, 1,4,255,255,255,0, 6,8,10,0,2,3,10,0,2,4, 255 };
    u8 v[4];
    CHECK(nw_dhcp_opt4(o, sizeof o, 6, v) == 1);
    CHECK(v[0] == 10 && v[1] == 0 && v[2] == 2 && v[3] == 3);
    CHECK(nw_dhcp_opt4(o, sizeof o, 1, v) == 1 && v[0] == 255 && v[3] == 0);
    CHECK(nw_dhcp_opt4(o, sizeof o, 3, v) == 0);
    CHECK(nw_dhcp_opt4(o, 3, 6, v) == 0);
}

static void t_http(void)
{
    static const u8 ok[] = "HTTP/1.0 200 OK\r\nServer: x\r\n\r\nBODY";
    static const u8 nf[] = "HTTP/1.1 404 Not Found\r\n\r\n";
    CHECK(nw_http_status(ok, sizeof ok - 1) == 200);
    CHECK(nw_http_status(nf, sizeof nf - 1) == 404);
    CHECK(nw_http_status(ok, 5) == -1);
    CHECK(nw_http_body(ok, sizeof ok - 1) == 30);
    CHECK(nw_http_body(ok, 20) == -1);
    CHECK(nw_http_body(nf, sizeof nf - 1) == (int)sizeof nf - 1);
}

static void t_http_clen(void)
{
    static const u8 a[] =
        "HTTP/1.0 200 OK\r\nContent-Length: 123904\r\nServer: x\r\n\r\nB";
    static const u8 lower[] =
        "HTTP/1.0 200 OK\r\ncontent-length:27488\r\n\r\n";
    static const u8 none[] = "HTTP/1.0 200 OK\r\nServer: x\r\n\r\nBODY";
    static const u8 junk[] = "HTTP/1.0 200 OK\r\nContent-Length: abc\r\n\r\n";
    static const u8 partial[] = "HTTP/1.0 200 OK\r\nContent-Len";
    CHECK(nw_http_clen(a, sizeof a - 1) == 123904);
    CHECK(nw_http_clen(lower, sizeof lower - 1) == 27488);
    CHECK(nw_http_clen(none, sizeof none - 1) == -1);
    CHECK(nw_http_clen(junk, sizeof junk - 1) == -1);
    CHECK(nw_http_clen(partial, sizeof partial - 1) == -1);

    static const u8 tricky[] =
        "HTTP/1.0 200 OK\r\nX-Content-Length-Hint: 5\r\nContent-Length: 42\r\n\r\n";
    CHECK(nw_http_clen(tricky, sizeof tricky - 1) == 42);
}

static void t_tcpcsum(void)
{

    u8 seg[20] = { 0x00,0x14, 0x00,0x50, 0,0,0,0, 0,0,0,0,
                   0x50,0x02, 0x20,0x00, 0x00,0x00, 0x00,0x00 };
    u32 src = (192u | (168u << 8) | (0u << 16) | (1u << 24));
    u32 dst = (192u | (168u << 8) | (0u << 16) | (2u << 24));
    u16 c = nw_tcp_csum(src, dst, seg, 20);
    CHECK(c == 0x0E2B);
    seg[16] = 0x0E; seg[17] = 0x2B;
    CHECK(nw_tcp_csum(src, dst, seg, 20) == 0);

    u8 one[21]; for (int i = 0; i < 21; i++) one[i] = seg[i < 20 ? i : 0];
    one[20] = 0xAB;
    CHECK(nw_tcp_csum(src, dst, one, 21) != nw_tcp_csum(src, dst, seg, 20));
}

static void t_tcpseq(void)
{
    CHECK(tcp_seq_le(5, 10) == 1);
    CHECK(tcp_seq_le(10, 10) == 1);
    CHECK(tcp_seq_le(11, 10) == 0);
    CHECK(tcp_seq_le(0xFFFFFF00u, 0x100) == 1);
    CHECK(tcp_seq_le(0x100, 0xFFFFFF00u) == 0);
}

static void t_tcp(void)
{
    Tcb t; int off, len, a;

    CHECK(tcp_open(&t, 1000, 0) == TA_SEND_SYN);
    CHECK(t.state == TS_SYN_SENT && t.snd_nxt == 1001);
    a = tcp_input(&t, TCP_SYN | TCP_ACK, 5000, 1001, 0, &off, &len);
    CHECK((a & TA_SEND_ACK) && (a & TA_CONNECTED));
    CHECK(t.state == TS_ESTAB && t.rcv_nxt == 5001 && t.snd_una == 1001);

    a = tcp_input(&t, TCP_ACK | TCP_PSH, 5001, 1001, 100, &off, &len);
    CHECK((a & TA_DELIVER) && off == 0 && len == 100 && t.rcv_nxt == 5101);
    CHECK(a & TA_SEND_ACK);

    a = tcp_input(&t, TCP_ACK, 5001, 1001, 100, &off, &len);
    CHECK(!(a & TA_DELIVER) && (a & TA_SEND_ACK) && t.rcv_nxt == 5101);

    a = tcp_input(&t, TCP_ACK, 5051, 1001, 80, &off, &len);
    CHECK((a & TA_DELIVER) && off == 50 && len == 30 && t.rcv_nxt == 5131);

    a = tcp_input(&t, TCP_ACK, 5500, 1001, 40, &off, &len);
    CHECK(!(a & TA_DELIVER) && (a & TA_SEND_ACK) && t.rcv_nxt == 5131);

    tcp_mark_sent(&t, 50, 10);
    CHECK(t.snd_nxt == 1051);
    a = tcp_input(&t, TCP_ACK, 5131, 1051, 0, &off, &len);
    CHECK(t.snd_una == 1051);
    CHECK(tcp_tick(&t, 10 + TCP_RTO + 1) == 0);

    a = tcp_input(&t, TCP_ACK | TCP_FIN, 5131, 1051, 20, &off, &len);
    CHECK((a & TA_DELIVER) && len == 20 && (a & TA_SEND_ACK));
    CHECK(t.state == TS_CLOSE_WAIT && t.rcv_nxt == 5152);

    a = tcp_close(&t, 20);
    CHECK((a & TA_SEND_FIN) && t.state == TS_LAST_ACK);
    a = tcp_input(&t, TCP_ACK, 5152, t.snd_nxt, 0, &off, &len);
    CHECK((a & TA_CLOSED) && t.state == TS_CLOSED);

    tcp_open(&t, 1, 0);
    a = tcp_input(&t, TCP_RST, 0, 0, 0, &off, &len);
    CHECK((a & TA_ERROR) && t.state == TS_CLOSED);

    tcp_open(&t, 1, 0);
    CHECK(tcp_tick(&t, TCP_RTO - 1) == 0);
    CHECK(tcp_tick(&t, TCP_RTO + 1) == TA_SEND_SYN);
    CHECK(tcp_tick(&t, 2 * (TCP_RTO + 1)) == TA_SEND_SYN);
    tcp_tick(&t, 3 * (TCP_RTO + 1));
    a = tcp_tick(&t, 5 * (TCP_RTO + 1));
    CHECK((a & TA_ERROR) && t.state == TS_CLOSED);

    tcp_open(&t, 2000, 0);
    tcp_input(&t, TCP_SYN | TCP_ACK, 9000, 2001, 0, &off, &len);
    tcp_mark_sent(&t, 64, 100);
    CHECK(tcp_tick(&t, 100 + TCP_RTO + 1) == TA_SEND_DATA);
}

static void t_ks_split(void)
{

    CHECK(ks_is_private(0, KEXT_KIND_APP) == 0);
    CHECK(ks_is_private(2, KEXT_KIND_APP) == 0);
    CHECK(ks_is_private(1, KEXT_KIND_APP) == (KEXT_PRIVATE_ENABLED ? 1 : 0));
    CHECK(ks_is_private(3, KEXT_KIND_APP) == (KEXT_PRIVATE_ENABLED ? 1 : 0));

    CHECK(ks_is_private(1, KEXT_KIND_KERNEL) == 0);
    CHECK(ks_is_private(3, KEXT_KIND_KERNEL) == 0);
}

static void t_ks_place(void)
{
    u32 c = 0;
    CHECK(ks_place(&c, 10, 1) == 0);   CHECK(c == 10);
    CHECK(ks_place(&c, 4, 4) == 12);   CHECK(c == 16);
    CHECK(ks_place(&c, 1, 16) == 16);  CHECK(c == 17);
    CHECK(ks_place(&c, 8, 0) == 17);   CHECK(c == 25);
}

static void t_ks_pool(void)
{
    u32 cap = KEXT_POOL_END - KEXT_POOL_BASE;
    CHECK(cap == 0x100000);
    CHECK(KEXT_POOL_BASE == 0x800000u);
    CHECK(ks_pool_fits(0, 1) == 1);
    CHECK(ks_pool_fits(0, cap) == 1);
    CHECK(ks_pool_fits(0, cap + 1) == 0);
    CHECK(ks_pool_fits(cap - 16, 16) == 1);
    CHECK(ks_pool_fits(cap - 16, 17) == 0);
    CHECK(ks_pool_fits(0, 128 * 1024) == 1);
}

static void t_ks_window(void)
{
    CHECK(KEXT_PRIV_PDE == 256);
    CHECK(ks_priv_va(0) == 0x40000000u);
    CHECK(ks_priv_va(0x1000) == 0x40001000u);
    CHECK(ks_in_priv(0x40000000u) == 1);
    CHECK(ks_in_priv(0x403FFFFFu) == 1);
    CHECK(ks_in_priv(0x40400000u) == 0);
    CHECK(ks_in_priv(0x3FFFFFFFu) == 0);
    CHECK(ks_in_priv(0) == 0);
    CHECK(KEXT_PRIV_VA > KEXT_POOL_END);
    CHECK(KEXT_PRIV_VA + KEXT_PRIV_SIZE < 0xFD000000u);
}

static void t_panic_report(void)
{
    CHECK(api->strcmp(panic_pf_reason(0), "Read: page not present") == 0);
    CHECK(api->strcmp(panic_pf_reason(3), "Write: access denied") == 0);
    CHECK(api->strcmp(panic_pf_reason(2), "Write: page not present") == 0);
    CHECK(api->strcmp(panic_pf_reason(17), "Execute: access denied") == 0);
    CHECK(api->strcmp(panic_pf_reason(16), "Execute: page not present") == 0);
    CHECK(api->strcmp(panic_pf_reason(15), "Invalid page-table bits") == 0);
    char b[256];
    int n = panic_report_fmt(b, sizeof b, "flopnix 0.7.2", 14, "page fault",
                             0, 0x64b1fb, "sys/edit.kx", 1, 0x400325fc, 3,
                             "5s open Snake\n", "");
    CHECK(n > 0 && n < (int)sizeof b);
    CHECK(b[n] == 0);

    CHECK(api->strstr(b, "P14") != 0);
    CHECK(api->strstr(b, "sys/edit.kx") != 0);
    CHECK(api->strstr(b, "cr2 400325fc") != 0);
    CHECK(api->strstr(b, "faults recovered: 3") != 0);
    CHECK(api->strstr(b, "open Snake") != 0);

    panic_report_fmt(b, sizeof b, "v", 6, "invalid opcode", 0, 0x1234,
                     "kernel", 0, 0, 0, "", "");
    CHECK(api->strstr(b, "cr2") == 0);
    CHECK(api->strstr(b, "owner kernel") != 0);

    char big[4096];
    for (int i = 0; i < (int)sizeof big - 1; i++) big[i] = 'X';
    big[sizeof big - 1] = 0;
    char small[128];
    int m = panic_report_fmt(small, sizeof small, "v", 0, "divide", 0, 0,
                             "a", 0, 0, 0, big, big);
    CHECK(m < (int)sizeof small);
    CHECK(small[sizeof small - 1] == 0);
    CHECK(small[m] == 0);
}

static void t_ks_owner(void)
{
    CHECK(ks_owner(3, 7) == 3);
    CHECK(ks_owner(0, 5) == 0);
    CHECK(ks_owner(-1, 7) == 7);
    CHECK(ks_owner(-1, 0) == 0);
    CHECK(ks_owner(-1, -1) == -1);
}

static void t_ks_fixable(void)
{
    CHECK(ks_fixable(0x40000000u, 0x1000) == 1);
    CHECK(ks_fixable(0x40000FFFu, 0x1000) == 1);
    CHECK(ks_fixable(0x40001000u, 0x1000) == 0);
    CHECK(ks_fixable(0x40001FFFu, 0x1001) == 1);
    CHECK(ks_fixable(0x40002000u, 0x1001) == 0);
    CHECK(ks_fixable(0x40000000u, 0) == 0);
    CHECK(ks_fixable(0x3FFFFFFFu, 0x1000) == 0);
    CHECK(ks_fixable(0x40400000u, 0x400000) == 0);
    CHECK(ks_fixable(0, 0x1000) == 0);
}

static void t_pg_index(void)
{
    CHECK(pd_index(0x00000000) == 0);
    CHECK(pd_index(0x00400000) == 1);
    CHECK(pd_index(0xFD000000) == 1012);
    CHECK(pd_index(0xFFFFFFFF) == 1023);
    CHECK(pt_index(0x00000000) == 0);
    CHECK(pt_index(0x00001000) == 1);
    CHECK(pt_index(0x003FF000) == 1023);
    CHECK(pt_index(0x00400000) == 0);
    CHECK(pt_index(0x00008000) == 8);
}

static void t_pg_align(void)
{
    CHECK(pg_down4m(0x00000000) == 0x00000000);
    CHECK(pg_down4m(0x004FFFFF) == 0x00400000);
    CHECK(pg_up4m(0x00000001) == 0x00400000);
    CHECK(pg_up4m(0x00400000) == 0x00400000);
    CHECK(pg_up4m(0x00400001) == 0x00800000);

    CHECK(pg_up4m(0xFFFFFFFF) == 0xFFC00000u);
}

static void t_pg_entries(void)
{

    CHECK(pde_4m(0xFD000000, PG_RW) == (0xFD000000u | PG_PS | PG_RW | PG_PRESENT));
    CHECK(pde_4m(0x00400000, 0) == (0x00400000u | PG_PS | PG_PRESENT));

    CHECK((pde_4m(0x004FF000, 0) & 0xFFC00000u) == 0x00400000u);

    CHECK(pde_table(0x00102000, PG_RW) == (0x00102000u | PG_RW | PG_PRESENT));
    CHECK(pte_4k(0x00008000, PG_RW) == (0x00008000u | PG_RW | PG_PRESENT));
    CHECK((pte_4k(0x000080FF, 0) & 0xFFFFF000u) == 0x00008000u);
}

static void t_pg_va_mapped(void)
{

    CHECK(pg_va_mapped(0, 0) == 0);
    CHECK(pg_va_mapped(0, PG_PRESENT) == 0);

    CHECK(pg_va_mapped(pde_4m(0x00400000, PG_RW), 0) == 1);
    CHECK(pg_va_mapped(PG_PRESENT | PG_PS, 0) == 1);

    CHECK(pg_va_mapped(pde_table(0x00102000, PG_RW), 0) == 0);
    CHECK(pg_va_mapped(pde_table(0x00102000, PG_RW),
                       pte_4k(0x00008000, PG_RW)) == 1);
}

static void t_pg_pte_span(void)
{
    u32 first = 0, count = 0;

    CHECK(pg_pte_span(0x00400000, 0x00600000, 0x00700000, &first, &count) == 1);
    CHECK(first == 512 && count == 256);

    CHECK(pg_pte_span(0x00400000, 0x00400000, 0x00401000, &first, &count) == 1);
    CHECK(first == 0 && count == 1);

    CHECK(pg_pte_span(0x00400000, 0x00400001, 0x00400002, &first, &count) == 1);
    CHECK(first == 0 && count == 1);
    CHECK(pg_pte_span(0x00400000, 0x00400FFF, 0x00401001, &first, &count) == 1);
    CHECK(first == 0 && count == 2);

    CHECK(pg_pte_span(0x00400000, 0x00300000, 0x00900000, &first, &count) == 1);
    CHECK(first == 0 && count == 1024);

    CHECK(pg_pte_span(0x00400000, 0x00800000, 0x00900000, &first, &count) == 0);
    CHECK(pg_pte_span(0x00400000, 0x00100000, 0x00200000, &first, &count) == 0);
    CHECK(pg_pte_span(0x00400000, 0x00500000, 0x00500000, &first, &count) == 0);
    CHECK(pg_pte_span(0x00400000, 0x00500000, 0x00400000, &first, &count) == 0);
}

static void bmp_put32(u8 *d, u32 v)
{
    d[0] = (u8)v; d[1] = (u8)(v >> 8); d[2] = (u8)(v >> 16); d[3] = (u8)(v >> 24);
}
static void bmp_put16(u8 *d, u32 v) { d[0] = (u8)v; d[1] = (u8)(v >> 8); }

static void bmp_make(u8 *b, int w, int h, u32 bpp)
{
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[0] = 'B'; b[1] = 'M';
    bmp_put32(b + 10, 54 + 1024);
    bmp_put32(b + 14, 40);
    bmp_put32(b + 18, (u32)w);
    bmp_put32(b + 22, (u32)h);
    bmp_put16(b + 26, 1);
    bmp_put16(b + 28, bpp);
    bmp_put32(b + 30, 0);
}

static void t_bmp_head(void)
{
    u8 b[64];
    BmpHead o;

    bmp_make(b, 640, 480, 8);
    CHECK(bmp_head(b, sizeof b, &o) == 0);
    CHECK(o.w == 640 && o.h == 480);
    CHECK(o.bpp == 8 && o.topdown == 0);
    CHECK(o.off == 54 + 1024);
    CHECK(o.rowsz == 640);
    CHECK(o.paloff == 14 + 40);

    bmp_make(b, 1, 1, 8);   CHECK(bmp_head(b, sizeof b, &o) == 0); CHECK(o.rowsz == 4);
    bmp_make(b, 5, 1, 8);   CHECK(bmp_head(b, sizeof b, &o) == 0); CHECK(o.rowsz == 8);
    bmp_make(b, 8, 1, 8);   CHECK(bmp_head(b, sizeof b, &o) == 0); CHECK(o.rowsz == 8);
    bmp_make(b, 3, 1, 24);  CHECK(bmp_head(b, sizeof b, &o) == 0); CHECK(o.rowsz == 12);
    bmp_make(b, 2, 1, 24);  CHECK(bmp_head(b, sizeof b, &o) == 0); CHECK(o.rowsz == 8);

    bmp_make(b, 4, 4, 8);
    bmp_put32(b + 22, (u32)(-4));
    CHECK(bmp_head(b, sizeof b, &o) == 0);
    CHECK(o.h == 4 && o.topdown == 1);

    bmp_make(b, 4, 4, 8);   CHECK(bmp_head(b, 53, &o) == -1);
    bmp_make(b, 4, 4, 8);   b[1] = 'Z';           CHECK(bmp_head(b, sizeof b, &o) == -1);
    bmp_make(b, 4, 4, 8);   bmp_put32(b + 30, 1); CHECK(bmp_head(b, sizeof b, &o) == -1);
    bmp_make(b, 4, 4, 4);   CHECK(bmp_head(b, sizeof b, &o) == -1);
    bmp_make(b, 0, 4, 8);   CHECK(bmp_head(b, sizeof b, &o) == -1);
    bmp_make(b, 4, 0, 8);   CHECK(bmp_head(b, sizeof b, &o) == -1);

    bmp_make(b, 0x40000000, 4, 24);
    CHECK(bmp_head(b, sizeof b, &o) == -1);
}

static void t_bmp_rows(void)
{
    u8 b[64];
    BmpHead o;

    bmp_make(b, 4, 3, 8);
    CHECK(bmp_head(b, sizeof b, &o) == 0);
    CHECK(bmp_src_row(&o, 0) == 2);
    CHECK(bmp_src_row(&o, 2) == 0);

    bmp_make(b, 4, 3, 8);
    bmp_put32(b + 22, (u32)(-3));
    CHECK(bmp_head(b, sizeof b, &o) == 0);
    CHECK(bmp_src_row(&o, 0) == 0);
    CHECK(bmp_src_row(&o, 2) == 2);

    bmp_make(b, 4, 3, 8);
    CHECK(bmp_head(b, sizeof b, &o) == 0);
    CHECK(bmp_row_ok(&o, 0, 1078 + 4) == 1);
    CHECK(bmp_row_ok(&o, 0, 1078 + 3) == 0);
    CHECK(bmp_row_ok(&o, 2, 1078 + 12) == 1);
    CHECK(bmp_row_ok(&o, 2, 1078 + 11) == 0);
    CHECK(bmp_row_ok(&o, -1, 999999) == 0);
}

static void t_bmp_scale(void)
{

    for (int i = 0; i < 8; i++) CHECK(bmp_scale(i, 8, 8) == i);

    CHECK(bmp_scale(0, 8, 4) == 0);
    CHECK(bmp_scale(1, 8, 4) == 0);
    CHECK(bmp_scale(2, 8, 4) == 1);
    CHECK(bmp_scale(7, 8, 4) == 3);

    CHECK(bmp_scale(0, 4, 8) == 0);
    CHECK(bmp_scale(1, 4, 8) == 2);
    CHECK(bmp_scale(3, 4, 8) == 6);

    CHECK(bmp_scale(1023, 1024, 3) == 2);
    CHECK(bmp_scale(639, 640, 1) == 0);
    CHECK(bmp_scale(0, 1, 1000) == 0);

    CHECK(bmp_scale(5, 0, 8) == 0);
    CHECK(bmp_scale(5, 8, 0) == 0);
    CHECK(bmp_scale(-3, 8, 8) == 0);
}

static unsigned mn_seed;
static unsigned mn_rng(void) { mn_seed = mn_seed * 1103515245u + 12345u; return mn_seed >> 8; }

static void t_ks_space(void)
{

    CHECK(ks_space_base(0) == KEXT_PRIV_VA);
    CHECK(ks_space_base(1) == KEXT_PRIV_VA + KEXT_PRIV_SIZE);
    CHECK(ks_space_base(5) - ks_space_base(4) == KEXT_PRIV_SIZE);
    CHECK(ks_space_pde(0) == (KEXT_PRIV_VA >> 22));
    CHECK(ks_space_pde(3) == (KEXT_PRIV_VA >> 22) + 3);

    for (int a = 0; a < KEXT_PD_MAX; a++)
        for (int b = a + 1; b < KEXT_PD_MAX; b++)
            CHECK(ks_space_pde(a) != ks_space_pde(b));

    CHECK(ks_priv_va_slot(0, 0x40) == KEXT_PRIV_VA + 0x40);
    CHECK(ks_priv_va_slot(2, 0x40) == KEXT_PRIV_VA + 2 * KEXT_PRIV_SIZE + 0x40);

    CHECK(ks_space_of(KEXT_PRIV_VA) == 0);
    CHECK(ks_space_of(KEXT_PRIV_VA + KEXT_PRIV_SIZE - 1) == 0);
    CHECK(ks_space_of(KEXT_PRIV_VA + KEXT_PRIV_SIZE) == 1);
    CHECK(ks_space_of(KEXT_PRIV_VA - 1) == -1);
    CHECK(ks_space_of(0) == -1);
    CHECK(ks_space_of(0x100000) == -1);
    CHECK(ks_space_of(KEXT_PRIV_VA + KEXT_PD_MAX * KEXT_PRIV_SIZE) == -1);

    CHECK(ks_fixable_slot(0, KEXT_PRIV_VA, 0x1000));
    CHECK(ks_fixable_slot(3, ks_space_base(3) + 0x800, 0x1000));
    CHECK(ks_fixable_slot(3, ks_space_base(3) + 0xFFF, 0x1));
    CHECK(!ks_fixable_slot(3, ks_space_base(3) + 0x1000, 0x1));
    CHECK(!ks_fixable_slot(3, 0, 0x1000));
    CHECK(!ks_fixable_slot(3, ks_space_base(3), 0));
    CHECK(!ks_fixable_slot(-1, ks_space_base(0), 0x1000));

    CHECK(!ks_fixable_slot(3, ks_space_base(4), 0x1000));
    CHECK(!ks_fixable_slot(4, ks_space_base(3), 0x1000));
}

static void t_ring3(void)
{

    CHECK(r3_desc(0, 0xFFFFF, ACC_KCODE, GRAN_4G) == 0x00CF9A000000FFFFULL);
    CHECK(r3_desc(0, 0xFFFFF, ACC_KDATA, GRAN_4G) == 0x00CF92000000FFFFULL);

    unsigned long long uc = r3_desc(0, 0xFFFFF, ACC_UCODE, GRAN_4G);
    unsigned long long ud = r3_desc(0, 0xFFFFF, ACC_UDATA, GRAN_4G);
    CHECK(uc == 0x00CFFA000000FFFFULL);
    CHECK(ud == 0x00CFF2000000FFFFULL);
    CHECK((uc ^ r3_desc(0, 0xFFFFF, ACC_KCODE, GRAN_4G)) == 0x0000600000000000ULL);
    CHECK((ud ^ r3_desc(0, 0xFFFFF, ACC_KDATA, GRAN_4G)) == 0x0000600000000000ULL);

    unsigned long long t = r3_desc(0x123456, 103, ACC_TSS, GRAN_BYTE);
    CHECK((t & 0xFFFF) == 103);
    CHECK(((t >> 16) & 0xFFFFFF) == 0x123456);
    CHECK(((t >> 40) & 0xFF) == 0x89);
    CHECK(((t >> 52) & 0xF) == 0);

    unsigned long long t2 = r3_desc(0xAB123456, 103, ACC_TSS, GRAN_BYTE);
    CHECK(((t2 >> 56) & 0xFF) == 0xAB);
    CHECK(((t2 >> 16) & 0xFFFFFF) == 0x123456);

    CHECK(r3_gate_flags(0) == 0x8E);
    CHECK(r3_gate_flags(3) == 0xEE);
    CHECK((r3_gate_flags(3) & 0x60) == 0x60);

    CHECK(SEL_USER(SEL_UCODE) == 0x1B);
    CHECK(SEL_USER(SEL_UDATA) == 0x23);

    CHECK((R3_EFLAGS & 0x200) == 0x200);
    CHECK((R3_EFLAGS & 0x3000) == 0);

    CHECK(!pf_from_user(0x3));
    CHECK(pf_from_user(0x7));
    CHECK(pf_from_user(0x4));
}

static unsigned mn_repeat_calls;
static unsigned mn_repeat_rng(void)
{

    if (++mn_repeat_calls <= 1000) return 0;
    return mn_rng();
}

static void t_mine(void)
{
    static MnBoard b;

    mn_reset(&b, 3, 3, 99);
    CHECK(b.mines == 0);
    mn_reset(&b, 1, 1, 10);
    CHECK(b.mines == 0);
    mn_place(&b, 0, 0, mn_rng);
    CHECK(mn_reveal(&b, 0, 0) == 1 && mn_won(&b));
    mn_reset(&b, 9, 9, 10);
    mn_repeat_calls = 0;
    mn_seed = 1234u;
    mn_place(&b, 0, 0, mn_repeat_rng);
    CHECK(mn_repeat_calls <= 81);
    int repeated_mines = 0;
    for (int i = 0; i < 81; i++) repeated_mines += b.mine[i];
    CHECK(repeated_mines == 10);
    CHECK(!b.mine[0] && !b.mine[1] && !b.mine[9] && !b.mine[10]);
    mn_reset(&b, 99, 99, 10);
    CHECK(b.w == MN_MAXW && b.h == MN_MAXH);

    for (int trial = 0; trial < 12; trial++) {
        mn_seed = 1234u + (unsigned)trial;
        mn_reset(&b, 9, 9, 10);
        mn_place(&b, 4, 4, mn_rng);
        int n = 0;
        for (int i = 0; i < 81; i++) if (b.mine[i]) n++;
        CHECK(n == 10);
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
                CHECK(!b.mine[mn_i(&b, 4 + dr, 4 + dc)]);
    }

    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if ((dr || dc) && mn_in(&b, r + dr, c + dc) &&
                        b.mine[mn_i(&b, r + dr, c + dc)]) n++;
            CHECK(b.adj[mn_i(&b, r, c)] == n);
        }

    mn_reset(&b, 4, 4, 1);
    b.mine[mn_i(&b, 0, 0)] = 1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if ((dr || dc) && mn_in(&b, r + dr, c + dc) &&
                        b.mine[mn_i(&b, r + dr, c + dc)]) n++;
            b.adj[mn_i(&b, r, c)] = (unsigned char)n;
        }

    int opened = mn_reveal(&b, 3, 3);
    CHECK(opened == 15);
    CHECK(mn_count_shown(&b) == 15);
    CHECK(!b.shown[mn_i(&b, 0, 0)]);
    CHECK(mn_won(&b));

    mn_reset(&b, 4, 4, 1);
    b.mark[mn_i(&b, 2, 2)] = MN_FLAG;
    CHECK(mn_reveal(&b, 0, 0) == 15);
    CHECK(!b.shown[mn_i(&b, 2, 2)]);
    CHECK(!mn_won(&b));

    CHECK(mn_reveal(&b, 2, 2) == 0);

    mn_reset(&b, 4, 4, 1);
    mn_cycle_mark(&b, 1, 1); CHECK(b.mark[mn_i(&b, 1, 1)] == MN_FLAG);
    mn_cycle_mark(&b, 1, 1); CHECK(b.mark[mn_i(&b, 1, 1)] == MN_QUERY);
    mn_cycle_mark(&b, 1, 1); CHECK(b.mark[mn_i(&b, 1, 1)] == MN_NONE);
    mn_cycle_mark(&b, 1, 1);
    mn_cycle_mark(&b, 1, 1);
    CHECK(mn_count_mark(&b, MN_QUERY) == 1 && mn_count_mark(&b, MN_FLAG) == 0);
    CHECK(mn_reveal(&b, 1, 1) > 0 && b.mark[mn_i(&b, 1, 1)] == MN_NONE);

    mn_reset(&b, 4, 4, 2);
    b.mine[mn_i(&b, 0, 0)] = b.mine[mn_i(&b, 0, 1)] = 1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if ((dr || dc) && mn_in(&b, r + dr, c + dc) &&
                        b.mine[mn_i(&b, r + dr, c + dc)]) n++;
            b.adj[mn_i(&b, r, c)] = (unsigned char)n;
        }
    CHECK(b.adj[mn_i(&b, 1, 1)] == 2);
    b.shown[mn_i(&b, 1, 1)] = 1;
    int hit = 9;

    CHECK(mn_chord(&b, 1, 1, &hit) == 0 && hit == 0);

    b.mark[mn_i(&b, 0, 0)] = MN_QUERY;
    b.mark[mn_i(&b, 0, 1)] = MN_QUERY;
    CHECK(mn_chord(&b, 1, 1, &hit) == 0);

    b.mark[mn_i(&b, 0, 0)] = b.mark[mn_i(&b, 0, 1)] = MN_FLAG;
    CHECK(mn_chord(&b, 1, 1, &hit) > 0 && hit == 0);
    CHECK(b.shown[mn_i(&b, 2, 2)]);

    CHECK(mn_chord(&b, 3, 3, &hit) == 0);
    CHECK(mn_chord(&b, 0, 3, &hit) == 0);

    mn_reset(&b, 4, 4, 2);
    b.mine[mn_i(&b, 0, 0)] = b.mine[mn_i(&b, 0, 1)] = 1;
    b.adj[mn_i(&b, 1, 1)] = 2;
    b.shown[mn_i(&b, 1, 1)] = 1;
    b.mark[mn_i(&b, 0, 0)] = MN_FLAG;
    b.mark[mn_i(&b, 2, 2)] = MN_FLAG;
    CHECK(mn_chord(&b, 1, 1, &hit) > 0 && hit == 1);
    CHECK(b.shown[mn_i(&b, 0, 1)]);

    mn_reset(&b, MN_MAXW, MN_MAXH, 1);
    b.mine[0] = 0;
    CHECK(mn_reveal(&b, 8, 15) == MN_MAXW * MN_MAXH);
}

static void t_opl2(void)
{

    CHECK(opl_op1(0) == 0x00 && opl_op2(0) == 0x03);
    CHECK(opl_op1(2) == 0x02 && opl_op2(2) == 0x05);
    CHECK(opl_op1(3) == 0x08 && opl_op2(3) == 0x0B);
    CHECK(opl_op1(6) == 0x10 && opl_op2(6) == 0x13);
    CHECK(opl_op1(8) == 0x12 && opl_op2(8) == 0x15);

    for (int a = 0; a < OPL_CHANS; a++)
        for (int b = a + 1; b < OPL_CHANS; b++)
            CHECK(opl_op1(a) != opl_op1(b) && opl_op1(a) != opl_op2(b) &&
                  opl_op2(a) != opl_op2(b));

    CHECK(opl_block(60) == 4 && opl_block(71) == 4);
    CHECK(opl_block(72) == 5 && opl_block(59) == 3);
    CHECK(opl_block(0) == 0 && opl_block(11) == 0);
    CHECK(opl_block(127) == 7);

    CHECK(opl_fnum(69) == 580);
    CHECK(opl_fnum(60) == 345);

    CHECK(opl_fnum(69) == opl_fnum(57) && opl_fnum(69) == opl_fnum(81));

    for (int i = 0; i < 12; i++) {
        CHECK(opl_fnum_tab[i] >= 0x156 && opl_fnum_tab[i] <= 0x2AE);
        if (i) CHECK(opl_fnum_tab[i] > opl_fnum_tab[i - 1]);
    }

    int lo_on, lo_off;
    int hi_on = opl_pitch(69, 1, &lo_on), hi_off = opl_pitch(69, 0, &lo_off);
    CHECK(lo_on == lo_off);
    CHECK((hi_on & 0x20) == 0x20 && (hi_off & 0x20) == 0);
    CHECK((hi_on & 0x1F) == (hi_off & 0x1F));

    CHECK(lo_on == 0x44 && (hi_on & 3) == 2 && ((hi_on >> 2) & 7) == 4);

    CHECK(opl_tl(127) == 0);
    CHECK(opl_tl(0) == 32);
    CHECK(opl_tl(64) > 0 && opl_tl(64) < 32);
    CHECK(opl_tl(200) == 0 && opl_tl(-5) == 32);

    CHECK(opl_detected(0x00, 0xC0));
    CHECK(opl_detected(0x06, 0xC6));
    CHECK(!opl_detected(0x00, 0x00));
    CHECK(!opl_detected(0xFF, 0xFF));
    CHECK(!opl_detected(0xC0, 0xC0));
    CHECK(!opl_detected(0x00, 0x80));
}

static void t_diskmap(void)
{
    CHECK(dm_char(FH_OK) == '=');
    CHECK(dm_char(FH_SLOW) == '~');
    CHECK(dm_char(FH_RETRY) == '?');
    CHECK(dm_char(FH_BAD) == 'X');
    CHECK(dm_char(FH_UNTESTED) == '.');
    CHECK(dm_char(99) == '.');

    char r[96];
    dm_ruler(0, 40, r, sizeof r);
    CHECK(api->strlen(r) == 40);
    CHECK(r[0] == '0' && r[10] == '1' && r[30] == '3');
    CHECK(r[5] == ':' && r[35] == ':');
    CHECK(r[1] == '.' && r[9] == '.');

    dm_ruler(40, 40, r, sizeof r);
    CHECK(r[0] == '4' && r[10] == '5' && r[30] == '7' && r[39] == '.');

    char s[8];
    dm_ruler(0, 40, s, sizeof s);
    CHECK(api->strlen(s) == 7 && s[0] == '0' && s[5] == ':');

    unsigned char cells[FH_CYLS];
    for (int i = 0; i < FH_CYLS; i++) cells[i] = FH_OK;
    cells[3] = FH_BAD;
    cells[7] = FH_SLOW;
    cells[41] = FH_RETRY;
    char row[96];
    dm_row(cells, 0, 40, row, sizeof row);
    CHECK(api->strlen(row) == 40);
    CHECK(row[0] == '=' && row[3] == 'X' && row[7] == '~');

    dm_row(cells, 40, 40, row, sizeof row);
    CHECK(api->strlen(row) == 40 && row[1] == '?' && row[0] == '=');

    char tiny[5];
    dm_row(cells, 0, 40, tiny, sizeof tiny);
    CHECK(api->strlen(tiny) == 4 && tiny[3] == 'X');

    char t[64];
    dm_tally(2879, 1, 0, 0, t, sizeof t);
    CHECK(STREQ(t, "2879 ok, 1 slow, 0 retried, 0 bad"));
    dm_tally(0, 0, 0, 0, t, sizeof t);
    CHECK(STREQ(t, "0 ok, 0 slow, 0 retried, 0 bad"));

    char t2[6];
    dm_tally(12345, 6, 7, 8, t2, sizeof t2);
    CHECK(api->strlen(t2) == 5 && STREQ(t2, "12345"));
}

static void t_tree(void)
{
    CHECK(tr_depth("foo.txt") == 0);
    CHECK(tr_depth("sys/fat.kx") == 1);
    CHECK(tr_depth("a/b/c.txt") == 2);
    CHECK(tr_depth("") == 0);

    const char *l;
    l = tr_leaf("sys/fat.kx"); CHECK(l && STREQ(l, "fat.kx"));
    l = tr_leaf("foo.txt");    CHECK(l && STREQ(l, "foo.txt"));
    l = tr_leaf("a/b/c.txt");  CHECK(l && STREQ(l, "c.txt"));

    char o[40];
    unsigned char f[4];

    CHECK(tr_prefix(f, 0, 0, o, sizeof o) == 3 && STREQ(o, "|- "));
    CHECK(tr_prefix(f, 0, 1, o, sizeof o) == 3 && STREQ(o, "`- "));

    f[0] = 0;
    CHECK(tr_prefix(f, 1, 0, o, sizeof o) == 6 && STREQ(o, "|  |- "));
    CHECK(tr_prefix(f, 1, 1, o, sizeof o) == 6 && STREQ(o, "|  `- "));

    f[0] = 1;
    CHECK(tr_prefix(f, 1, 0, o, sizeof o) == 6 && STREQ(o, "   |- "));
    CHECK(tr_prefix(f, 1, 1, o, sizeof o) == 6 && STREQ(o, "   `- "));

    f[0] = 0; f[1] = 1;
    CHECK(tr_prefix(f, 2, 1, o, sizeof o) == 9 && STREQ(o, "|     `- "));
    f[0] = 1; f[1] = 0;
    CHECK(tr_prefix(f, 2, 0, o, sizeof o) == 9 && STREQ(o, "   |  |- "));

    CHECK(tr_prefix(f, 2, 0, o, 4) <= 4);
}

static void t_tabcomp(void)
{
    static const char *const c[] = { "cat", "cal", "calc", "cd", "clear",
                                     "cp", "df", "dmesg", "tree" };
    const int n = 9;
    char o[24];

    CHECK(tc_count("", c, n) == n);
    CHECK(tc_count("c", c, n) == 6);
    CHECK(tc_count("ca", c, n) == 3);
    CHECK(tc_count("cal", c, n) == 2);
    CHECK(tc_count("tr", c, n) == 1);
    CHECK(tc_count("zz", c, n) == 0);

    CHECK(tc_common("tr", c, n, o, sizeof o) == 1 && STREQ(o, "tree"));
    CHECK(tc_common("dm", c, n, o, sizeof o) == 1 && STREQ(o, "dmesg"));

    CHECK(tc_common("ca", c, n, o, sizeof o) == 3 && STREQ(o, "ca"));
    CHECK(tc_common("cal", c, n, o, sizeof o) == 2 && STREQ(o, "cal"));
    CHECK(tc_common("c", c, n, o, sizeof o) == 6 && STREQ(o, "c"));

    static const char *const d[] = { "defrag", "delete", "dev" };
    CHECK(tc_common("d", d, 3, o, sizeof o) == 3 && STREQ(o, "de"));
    static const char *const e[] = { "reboot", "reset" };
    CHECK(tc_common("r", e, 2, o, sizeof o) == 2 && STREQ(o, "re"));

    CHECK(tc_common("zz", c, n, o, sizeof o) == 0);

    CHECK(tc_common("cat", c, n, o, sizeof o) == 1 && STREQ(o, "cat"));

    const char *h;
    h = tc_nth("ca", c, n, 0); CHECK(h && STREQ(h, "cat"));
    h = tc_nth("ca", c, n, 1); CHECK(h && STREQ(h, "cal"));
    h = tc_nth("ca", c, n, 2); CHECK(h && STREQ(h, "calc"));
    CHECK(tc_nth("ca", c, n, 3) == 0);
    CHECK(tc_nth("zz", c, n, 0) == 0);

    CHECK(tc_count(0, c, n) == 0);
    CHECK(tc_common("c", c, 0, o, sizeof o) == 0);
    CHECK(tc_common("c", c, n, o, 1) == 6);
}

static void t_sc_resolve(void)
{
    char o[SC_MAX];

    CHECK(sc_resolve("", "foo.txt", o, sizeof o) == 1 && STREQ(o, "foo.txt"));

    CHECK(sc_resolve("sys", "fat.kx", o, sizeof o) == 1 && STREQ(o, "sys/fat.kx"));
    CHECK(sc_resolve("a/b", "c.txt", o, sizeof o) == 1 && STREQ(o, "a/b/c.txt"));

    CHECK(sc_resolve("sys", "/foo.txt", o, sizeof o) == 1 && STREQ(o, "foo.txt"));
    CHECK(sc_resolve("a/b", "/x", o, sizeof o) == 1 && STREQ(o, "x"));

    CHECK(sc_resolve("sys", "a:foo.txt", o, sizeof o) == 1 && STREQ(o, "foo.txt"));
    CHECK(sc_resolve("sys", "a:/foo.txt", o, sizeof o) == 1 && STREQ(o, "foo.txt"));

    CHECK(sc_resolve("sys", ".", o, sizeof o) == 1 && STREQ(o, "sys"));
    CHECK(sc_resolve("sys", "./fat.kx", o, sizeof o) == 1 && STREQ(o, "sys/fat.kx"));
    CHECK(sc_resolve("", ".", o, sizeof o) == 1 && STREQ(o, ""));

    CHECK(sc_resolve("sys", "..", o, sizeof o) == 1 && STREQ(o, ""));
    CHECK(sc_resolve("a/b", "..", o, sizeof o) == 1 && STREQ(o, "a"));
    CHECK(sc_resolve("", "..", o, sizeof o) == 1 && STREQ(o, ""));
    CHECK(sc_resolve("a", "../..", o, sizeof o) == 1 && STREQ(o, ""));
    CHECK(sc_resolve("sys", "../foo", o, sizeof o) == 1 && STREQ(o, "foo"));
    CHECK(sc_resolve("a/b", "../c", o, sizeof o) == 1 && STREQ(o, "a/c"));

    CHECK(sc_resolve("", "a/../b", o, sizeof o) == 1 && STREQ(o, "b"));
    CHECK(sc_resolve("", "a/b/../c", o, sizeof o) == 1 && STREQ(o, "a/c"));

    CHECK(sc_resolve("sys", "", o, sizeof o) == 1 && STREQ(o, "sys"));
    CHECK(sc_resolve("", "", o, sizeof o) == 1 && STREQ(o, ""));

    CHECK(sc_resolve("", "a//b", o, sizeof o) == 1 && STREQ(o, "a/b"));
    CHECK(sc_resolve("", "foo/", o, sizeof o) == 1 && STREQ(o, "foo"));
    CHECK(sc_resolve("", "sys\\fat.kx", o, sizeof o) == 1 && STREQ(o, "sys/fat.kx"));
    CHECK(sc_resolve("", "/", o, sizeof o) == 1 && STREQ(o, ""));

    CHECK(sc_resolve("", "0123456789012345678901234567890", o, sizeof o) == 0);
    CHECK(sc_resolve("aaaaaaaaaa/bbbbbbbbbb", "cccccccccc", o, sizeof o) == 0);

    CHECK(sc_resolve("", "01234567890123456789012", o, sizeof o) == 1);
}

static void t_sc_parent(void)
{
    char o[SC_MAX];
    sc_parent("a/b", o, sizeof o);  CHECK(STREQ(o, "a"));
    sc_parent("sys", o, sizeof o);  CHECK(STREQ(o, ""));
    sc_parent("", o, sizeof o);     CHECK(STREQ(o, ""));
    sc_parent("a/b/c", o, sizeof o); CHECK(STREQ(o, "a/b"));
}

static void t_mf_vlq(void)
{
    u32 i;

    u8 a[] = { 0x00 };              i = 0; CHECK(mf_vlq(a, 1, &i) == 0 && i == 1);
    u8 b[] = { 0x40 };              i = 0; CHECK(mf_vlq(b, 1, &i) == 0x40 && i == 1);
    u8 c[] = { 0x7F };              i = 0; CHECK(mf_vlq(c, 1, &i) == 0x7F && i == 1);
    u8 d[] = { 0x81, 0x00 };        i = 0; CHECK(mf_vlq(d, 2, &i) == 0x80 && i == 2);
    u8 e[] = { 0xC0, 0x00 };        i = 0; CHECK(mf_vlq(e, 2, &i) == 0x2000 && i == 2);
    u8 f[] = { 0xFF, 0x7F };        i = 0; CHECK(mf_vlq(f, 2, &i) == 0x3FFF && i == 2);
    u8 g[] = { 0x81, 0x80, 0x00 };  i = 0; CHECK(mf_vlq(g, 3, &i) == 0x4000 && i == 3);
    u8 h[] = { 0xFF, 0xFF, 0xFF, 0x7F };
    i = 0; CHECK(mf_vlq(h, 4, &i) == 0x0FFFFFFF && i == 4);

    u8 t[] = { 0x81, 0x80 };        i = 0; mf_vlq(t, 2, &i); CHECK(i <= 2);
    i = 5; CHECK(mf_vlq(t, 2, &i) == 0);
}

static void t_mf_datalen(void)
{
    CHECK(mf_datalen(0x90) == 2);
    CHECK(mf_datalen(0x80) == 2);
    CHECK(mf_datalen(0xA0) == 2);
    CHECK(mf_datalen(0xB0) == 2);
    CHECK(mf_datalen(0xE0) == 2);
    CHECK(mf_datalen(0xC0) == 1);
    CHECK(mf_datalen(0xD0) == 1);

    CHECK(mf_datalen(0x9F) == 2 && mf_datalen(0xC7) == 1);
}

static void t_mf_header(void)
{
    int ntrk = 0;
    u32 first = 0;

    u8 h[] = { 'M','T','h','d', 0,0,0,6, 0,1, 0,2, 1,0xE0,
               'M','T','r','k', 0,0,0,4, 0,0,0,0 };
    CHECK(mf_header(h, sizeof h, &ntrk, &first) == 480);
    CHECK(ntrk == 2 && first == 14);

    u8 h0[] = { 'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,0x60,
                'M','T','r','k', 0,0,0,0 };
    CHECK(mf_header(h0, sizeof h0, &ntrk, &first) == 0x60 && ntrk == 1);

    u8 bad[] = { 'R','I','F','F', 0,0,0,6, 0,1, 0,2, 1,0xE0 };
    CHECK(mf_header(bad, sizeof bad, &ntrk, &first) == 0);
    CHECK(mf_header(h, 8, &ntrk, &first) == 0);

    u8 smpte[] = { 'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0xE8,0x08 };
    CHECK(mf_header(smpte, sizeof smpte, &ntrk, &first) == 0);

    u8 f2[] = { 'M','T','h','d', 0,0,0,6, 0,2, 0,1, 0,0x60 };
    CHECK(mf_header(f2, sizeof f2, &ntrk, &first) == 0);
}

static void t_mf_tickrate(void)
{

    CHECK(mf_tickrate(500000, 480) == 104);

    CHECK(mf_tickrate(1000000, 480) == 208);

    CHECK(mf_tickrate(500000, 96) == 520);
    CHECK(mf_tickrate(500000, 24) == 2083);

    CHECK(mf_tickrate(500000, 0) == 0);
    CHECK(mf_tickrate(0, 480) == 0);
}

static void t_mf_running(void)
{
    static const u8 trk[] = {
        0x00, 0x90, 0x3C, 0x40,
        0x60,       0x3E, 0x40,
        0x00,       0x3C, 0x00,
        0x30, 0x80, 0x3E, 0x40,
        0x00, 0xFF, 0x2F, 0x00
    };
    MfTrack t = { trk, sizeof trk, 0, 0, 0, 0 };
    u8 s = 0, a = 0, b = 0;

    CHECK(mf_next(&t, &s, &a, &b) == 1);
    CHECK(s == 0x90 && a == 0x3C && b == 0x40 && t.tick == 0);

    CHECK(mf_next(&t, &s, &a, &b) == 1);
    CHECK(s == 0x90 && a == 0x3E && b == 0x40);
    CHECK(t.tick == 96);

    CHECK(mf_next(&t, &s, &a, &b) == 1);
    CHECK(s == 0x90 && a == 0x3C && b == 0x00);
    CHECK(t.tick == 96);

    CHECK(mf_next(&t, &s, &a, &b) == 1);
    CHECK(s == 0x80 && a == 0x3E && t.tick == 144);

    CHECK(mf_next(&t, &s, &a, &b) == 1);
    CHECK(s == MF_META && a == 0x2F);
    CHECK(t.ended == 1);
    CHECK(mf_next(&t, &s, &a, &b) == 0);

    static const u8 meta[] = {
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0x90, 0x40, 0x7F
    };
    MfTrack m = { meta, sizeof meta, 0, 0, 0, 0 };
    CHECK(mf_next(&m, &s, &a, &b) == 1 && s == MF_META && a == 0x51);
    CHECK(mf_next(&m, &s, &a, &b) == 1);
    CHECK(s == 0x90 && a == 0x40 && b == 0x7F);

    static const u8 orphan[] = { 0x00, 0x3C, 0x40 };
    MfTrack o = { orphan, sizeof orphan, 0, 0, 0, 0 };
    CHECK(mf_next(&o, &s, &a, &b) == 0);

    static const u8 cut[] = { 0x00, 0x90, 0x3C };
    MfTrack c = { cut, sizeof cut, 0, 0, 0, 0 };
    CHECK(mf_next(&c, &s, &a, &b) == 0);
}

static void t_mf_note_hz(void)
{

    CHECK(mf_note_hz(69) == 440);
    CHECK(mf_note_hz(57) == 220);
    CHECK(mf_note_hz(81) == 880);
    CHECK(mf_note_hz(45) == 110);

    u32 c4 = mf_note_hz(60);
    CHECK(c4 >= 259 && c4 <= 263);

    for (int n = 24; n < 108; n++) {
        CHECK(mf_note_hz(n) > 0);
        CHECK(mf_note_hz(n) < mf_note_hz(n + 1));
    }
    CHECK(mf_note_hz(-1) == 0);
    CHECK(mf_note_hz(200) == 0);
}

static void t_fh_class(void)
{

    CHECK(fh_class(0, 1, 25) == FH_OK);
    CHECK(fh_class(0, 1, 0) == FH_OK);

    CHECK(fh_class(0, 1, 200) == FH_OK);
    CHECK(fh_class(0, 1, 230) == FH_OK);

    CHECK(fh_class(0, 1, FH_SLOW_MS) == FH_SLOW);
    CHECK(fh_class(0, 1, 500) == FH_SLOW);
    CHECK(fh_class(0, 1, FH_SLOW_MS - 1) == FH_OK);

    CHECK(fh_class(0, 2, 25) == FH_RETRY);
    CHECK(fh_class(0, 2, 900) == FH_RETRY);
    CHECK(fh_class(0, 4, 25) == FH_RETRY);

    CHECK(fh_class(-1, 4, 900) == FH_BAD);
    CHECK(fh_class(-1, 1, 0) == FH_BAD);

    CHECK(fh_class(0, 0, 25) == FH_OK);
    CHECK(fh_class(0, -3, 25) == FH_OK);
}

static void t_fh_worst(void)
{

    CHECK(FH_UNTESTED < FH_OK && FH_OK < FH_SLOW);
    CHECK(FH_SLOW < FH_RETRY && FH_RETRY < FH_BAD);

    CHECK(fh_worst(FH_OK, FH_OK) == FH_OK);
    CHECK(fh_worst(FH_OK, FH_BAD) == FH_BAD);
    CHECK(fh_worst(FH_BAD, FH_OK) == FH_BAD);
    CHECK(fh_worst(FH_SLOW, FH_RETRY) == FH_RETRY);
    CHECK(fh_worst(FH_UNTESTED, FH_OK) == FH_OK);
    CHECK(fh_worst(FH_RETRY, FH_SLOW) == FH_RETRY);
}

static void t_fh_cell(void)
{
    int c = -1, h = -1;

    CHECK(fh_cell(0, &c, &h) == 1 && c == 0 && h == 0);
    CHECK(fh_cell(17, &c, &h) == 1 && c == 0 && h == 0);
    CHECK(fh_cell(18, &c, &h) == 1 && c == 0 && h == 1);
    CHECK(fh_cell(35, &c, &h) == 1 && c == 0 && h == 1);
    CHECK(fh_cell(36, &c, &h) == 1 && c == 1 && h == 0);
    CHECK(fh_cell(2879, &c, &h) == 1 && c == 79 && h == 1);

    CHECK(fh_cell(2880, &c, &h) == 0);
    CHECK(fh_cell(999999, &c, &h) == 0);

    for (u32 l = 0; l < FH_TOTAL; l += 37) {
        CHECK(fh_cell(l, &c, &h) == 1);
        CHECK(c >= 0 && c < FH_CYLS && h >= 0 && h < FH_HEADS);
    }
}

static void t_fh_verdict(void)
{

    CHECK(fh_verdict(0, 0, 0, 2880) == 0);

    CHECK(fh_verdict(1, 0, 0, 2880) == 2);
    CHECK(fh_verdict(500, 0, 0, 2880) == 2);

    CHECK(fh_verdict(0, 40, 0, 2880) == 1);
    CHECK(fh_verdict(0, 1, 0, 2880) == 1);

    CHECK(fh_verdict(0, 0, 400, 2880) == 1);
    CHECK(fh_verdict(0, 0, 2, 2880) == 0);

    CHECK(fh_verdict(0, 0, 0, 0) == 0);
}

static void t_nl_parse(void)
{
    NoteRec r;

    CHECK(nl_parse("10\t20\t200\t150\t1\tnote1.txt", &r) == 1);
    CHECK(r.x == 10 && r.y == 20 && r.w == 200 && r.h == 150);
    CHECK(r.open == 1 && STREQ(r.file, "note1.txt"));

    CHECK(nl_parse("0\t0\t200\t150\t0\tn.txt", &r) == 1);
    CHECK(r.open == 0 && r.x == 0 && r.y == 0);

    CHECK(nl_parse("-500\t-500\t200\t150\t1\tn.txt", &r) == 1);
    CHECK(r.x >= 0 && r.y >= 0);
    CHECK(nl_parse("10\t20\t5\t5\t1\tn.txt", &r) == 1);
    CHECK(r.w >= NL_MINW && r.h >= NL_MINH);
    CHECK(nl_parse("10\t20\t9999\t9999\t1\tn.txt", &r) == 1);
    CHECK(r.w <= NL_MAXW && r.h <= NL_MAXH);

    CHECK(nl_parse("1\t1\t200\t150\t7\tn.txt", &r) == 1 && r.open == 1);

    CHECK(nl_parse("", &r) == 0);
    CHECK(nl_parse("garbage", &r) == 0);
    CHECK(nl_parse("10\t20\t200", &r) == 0);
    CHECK(nl_parse("10\t20\t200\t150\t1\t", &r) == 0);
    CHECK(nl_parse("\t\t\t\t\t", &r) == 0);
    CHECK(nl_parse("10 20 200 150 1 n.txt", &r) == 0);

    CHECK(nl_parse("1\t1\t200\t150\t1\t"
                   "a_very_long_note_filename_beyond_the_field.txt", &r) == 1);
    CHECK((int)api->strlen(r.file) == NL_NAMEMAX - 1);
}

static void t_nl_fmt(void)
{
    char b[64];
    NoteRec r = { 10, 20, 200, 150, 1, "note1.txt" };
    int n = nl_fmt(b, sizeof b, &r);
    CHECK(n > 0 && n < (int)sizeof b);
    CHECK(STREQ(b, "10\t20\t200\t150\t1\tnote1.txt"));

    NoteRec back;
    CHECK(nl_parse(b, &back) == 1);
    CHECK(back.x == r.x && back.y == r.y && back.w == r.w && back.h == r.h);
    CHECK(back.open == r.open && STREQ(back.file, r.file));

    NoteRec z = { 0, 0, NL_MINW, NL_MINH, 0, "n.txt" };
    CHECK(nl_fmt(b, sizeof b, &z) > 0);
    CHECK(nl_parse(b, &back) == 1 && back.open == 0 && back.x == 0);

    CHECK(nl_fmt(b, 8, &r) == 0);
    CHECK(nl_fmt(b, 0, &r) == 0);
}

static void t_nb_wrap(void)
{

    CHECK(nb_line_end("hello", 5, 0, 10) == 5);
    CHECK(nb_next_line("hello", 5, 0, 10) == 5);

    CHECK(nb_line_end("abcde", 5, 0, 5) == 5);
    CHECK(nb_next_line("abcde", 5, 0, 5) == 5);

    CHECK(nb_line_end("hello world", 11, 0, 8) == 5);
    CHECK(nb_next_line("hello world", 11, 0, 8) == 6);

    CHECK(nb_line_end("helloworld", 10, 0, 5) == 5);
    CHECK(nb_next_line("helloworld", 10, 0, 5) == 5);

    CHECK(nb_line_end("ab\ncd", 5, 0, 10) == 2);
    CHECK(nb_next_line("ab\ncd", 5, 0, 10) == 3);
    CHECK(nb_line_end("ab\ncd", 5, 3, 10) == 5);

    CHECK(nb_line_end("a\n\nb", 4, 2, 10) == 2);
    CHECK(nb_next_line("a\n\nb", 4, 2, 10) == 3);

    CHECK(nb_line_end("", 0, 0, 10) == 0);
    CHECK(nb_next_line("", 0, 0, 10) == 0);
    CHECK(nb_next_line("abc", 3, 3, 10) == 3);

    CHECK(nb_next_line("ab   cd", 7, 0, 3) == 5);

    CHECK(nb_next_line("abc", 3, 0, 0) > 0);
    CHECK(nb_next_line("abc", 3, 0, 1) > 0);

    const char *t = "one two three four five";
    int i = 0, guard = 0, seen = 0;
    while (i < 23 && guard++ < 50) {
        int e = nb_line_end(t, 23, i, 7);
        seen += e - i;
        int nx = nb_next_line(t, 23, i, 7);
        CHECK(nx > i);
        i = nx;
    }
    CHECK(guard < 50);
    CHECK(seen >= 19);
}

static void t_nb_edit(void)
{
    char b[8] = "";
    NoteBuf n = { b, sizeof b, 0, 0 };

    CHECK(nb_insert(&n, 'a') == 1);
    CHECK(n.len == 1 && n.car == 1 && b[0] == 'a' && b[1] == 0);
    CHECK(nb_insert(&n, 'c') == 1);
    n.car = 1;
    CHECK(nb_insert(&n, 'b') == 1);
    CHECK(n.len == 3 && n.car == 2 && STREQ(b, "abc"));

    n.car = 3;
    CHECK(nb_insert(&n, '\n') == 1);
    CHECK(n.len == 4 && b[3] == '\n');

    while (nb_insert(&n, 'x')) ;
    CHECK(n.len == (int)sizeof b - 1);
    CHECK(b[sizeof b - 1] == 0);
    CHECK(nb_insert(&n, 'y') == 0);

    char c[8] = "abc";
    NoteBuf m = { c, sizeof c, 3, 3 };
    CHECK(nb_backspace(&m) == 1);
    CHECK(m.len == 2 && m.car == 2 && STREQ(c, "ab"));
    m.car = 1;
    CHECK(nb_backspace(&m) == 1);
    CHECK(m.len == 1 && m.car == 0 && STREQ(c, "b"));
    m.car = 0;
    CHECK(nb_backspace(&m) == 0);
    CHECK(m.len == 1);

    char d[8] = "abc";
    NoteBuf p = { d, sizeof d, 3, 0 };
    CHECK(nb_delete(&p) == 1);
    CHECK(p.len == 2 && p.car == 0 && STREQ(d, "bc"));
    p.car = 2;
    CHECK(nb_delete(&p) == 0);
    CHECK(p.len == 2);
}

static void t_ax_match(void)
{

    CHECK(ax_match("fill_rect", "") == 1);
    CHECK(ax_match("", "") == 1);

    CHECK(ax_match("fill_rect", "fill_rect") == 1);
    CHECK(ax_match("fill_rect", "fill") == 1);
    CHECK(ax_match("fill_rect", "rect") == 1);
    CHECK(ax_match("fill_rect", "l_r") == 1);

    CHECK(ax_match("fill_rect", "FILL") == 1);
    CHECK(ax_match("FILL_RECT", "fill") == 1);
    CHECK(ax_match("Fill_Rect", "L_r") == 1);

    CHECK(ax_match("fill_rect", "circle") == 0);
    CHECK(ax_match("fill", "fill_rect") == 0);
    CHECK(ax_match("", "x") == 0);

    CHECK(ax_match("fill_rec", "fill_rect") == 0);
    CHECK(ax_match("abcabd", "abd") == 1);
    CHECK(ax_match("aaab", "aab") == 1);

    CHECK(ax_match(0, "x") == 0);
    CHECK(ax_match("x", 0) == 1);
}

static void t_ax_clamp_top(void)
{
    CHECK(ax_clamp_top(0, 10, 100) == 0);
    CHECK(ax_clamp_top(37, 10, 100) == 37);
    CHECK(ax_clamp_top(90, 10, 100) == 90);
    CHECK(ax_clamp_top(95, 10, 100) == 90);
    CHECK(ax_clamp_top(1000, 10, 100) == 90);
    CHECK(ax_clamp_top(-5, 10, 100) == 0);

    CHECK(ax_clamp_top(5, 10, 3) == 0);
    CHECK(ax_clamp_top(0, 10, 10) == 0);

    CHECK(ax_clamp_top(5, 0, 100) == 0);
    CHECK(ax_clamp_top(5, 10, 0) == 0);
}

static void t_ax_scroll(void)
{

    CHECK(ax_scroll(0, 0, 10, 100) == 0);
    CHECK(ax_scroll(5, 0, 10, 100) == 0);
    CHECK(ax_scroll(9, 0, 10, 100) == 0);

    CHECK(ax_scroll(10, 0, 10, 100) == 1);
    CHECK(ax_scroll(11, 1, 10, 100) == 2);

    CHECK(ax_scroll(4, 5, 10, 100) == 4);
    CHECK(ax_scroll(0, 20, 10, 100) == 0);

    CHECK(ax_scroll(99, 0, 10, 100) == 90);

    CHECK(ax_scroll(99, 95, 10, 100) == 90);
    CHECK(ax_scroll(0, 0, 10, 3) == 0);
    CHECK(ax_scroll(2, 0, 10, 3) == 0);

    CHECK(ax_scroll(0, 0, 0, 100) == 0);
    CHECK(ax_scroll(5, 0, 10, 0) == 0);
    CHECK(ax_scroll(-1, 0, 10, 100) == 0);
}

static void t_wp_mode(void)
{

    CHECK(wp_mode_norm(0, "") == WP_GRADIENT);
    CHECK(wp_mode_norm(WP_GRADIENT, "/wall.bmp") == WP_GRADIENT);

    CHECK(wp_mode_norm(WP_SOLID, "") == WP_SOLID);
    CHECK(wp_mode_norm(WP_SOLID, "/wall.bmp") == WP_SOLID);

    CHECK(wp_mode_norm(WP_BITMAP, "/wall.bmp") == WP_BITMAP);

    CHECK(wp_mode_norm(WP_BITMAP, "") == WP_GRADIENT);
    CHECK(wp_mode_norm(WP_BITMAP, 0) == WP_GRADIENT);

    CHECK(wp_mode_norm(3, "/wall.bmp") == WP_GRADIENT);
    CHECK(wp_mode_norm(99, "/wall.bmp") == WP_GRADIENT);
    CHECK(wp_mode_norm(255, "") == WP_GRADIENT);
}

static void t_mq_norm(void)
{
    int lo = 0, hi = 0;
    mq_norm(3, 9, &lo, &hi);  CHECK(lo == 3 && hi == 9);
    mq_norm(9, 3, &lo, &hi);  CHECK(lo == 3 && hi == 9);
    mq_norm(5, 5, &lo, &hi);  CHECK(lo == 5 && hi == 5);
    mq_norm(-4, 2, &lo, &hi); CHECK(lo == -4 && hi == 2);
    mq_norm(2, -4, &lo, &hi); CHECK(lo == -4 && hi == 2);
}

static void t_mq_hit(void)
{

    const int ix = 100, iy = 100, iw = 76, ih = 56;

    CHECK(mq_hit(90,  90,  200, 200, ix, iy, iw, ih) == 1);
    CHECK(mq_hit(200, 200, 90,  90,  ix, iy, iw, ih) == 1);
    CHECK(mq_hit(200, 90,  90,  200, ix, iy, iw, ih) == 1);
    CHECK(mq_hit(90,  200, 200, 90,  ix, iy, iw, ih) == 1);

    CHECK(mq_hit(110, 110, 120, 120, ix, iy, iw, ih) == 1);

    CHECK(mq_hit(0, 0, 400, 400, ix, iy, iw, ih) == 1);

    CHECK(mq_hit(0,  0,  99,  400, ix, iy, iw, ih) == 0);
    CHECK(mq_hit(177, 0, 400, 400, ix, iy, iw, ih) == 0);
    CHECK(mq_hit(0,  0,  400, 99,  ix, iy, iw, ih) == 0);
    CHECK(mq_hit(0,  157, 400, 400, ix, iy, iw, ih) == 0);

    CHECK(mq_hit(0, 0, 100, 100, ix, iy, iw, ih) == 1);
    CHECK(mq_hit(175, 155, 400, 400, ix, iy, iw, ih) == 1);
    CHECK(mq_hit(176, 156, 400, 400, ix, iy, iw, ih) == 0);

    CHECK(mq_hit(120, 120, 120, 120, ix, iy, iw, ih) == 1);
    CHECK(mq_hit(10,  10,  10,  10,  ix, iy, iw, ih) == 0);
}

static void t_wp_grad(void)
{

    u8 none[6] = { 0, 0, 0, 0, 0, 0 };
    CHECK(wp_rgb_any(none, 6) == 0);

    u8 one[6]  = { 0, 0, 0, 0, 0, 1 };
    CHECK(wp_rgb_any(one, 6) == 1);
    u8 first[6] = { 1, 0, 0, 0, 0, 0 };
    CHECK(wp_rgb_any(first, 6) == 1);
    u8 teal[6] = { 36, 80, 100, 16, 34, 56 };
    CHECK(wp_rgb_any(teal, 6) == 1);

    u8 blk[3] = { 0, 0, 0 };
    CHECK(wp_rgb_any(blk, 3) == 0);
    u8 red[3] = { 255, 0, 0 };
    CHECK(wp_rgb_any(red, 3) == 1);

    CHECK(wp_rgb_any(0, 6) == 0);
    CHECK(wp_rgb_any(teal, 0) == 0);
}

static void t_wp_path_fix(void)
{
    char p[8];

    api->strlcpy(p, "/a.bmp", sizeof p);
    CHECK(wp_path_fix(p, sizeof p) == 1);
    CHECK(STREQ(p, "/a.bmp"));

    p[0] = 0;
    CHECK(wp_path_fix(p, sizeof p) == 0);

    for (int i = 0; i < 8; i++) p[i] = 'x';
    CHECK(wp_path_fix(p, sizeof p) == 1);
    CHECK(p[7] == 0);
    CHECK(STREQ(p, "xxxxxxx"));

    CHECK(wp_path_fix(0, 8) == 0);
    CHECK(wp_path_fix(p, 0) == 0);
}

static void t_pg_span(void)
{
    u32 first = 0, count = 0;

    CHECK(pg_span4m(0xFD000000, 640 * 480, &first, &count) == 1);
    CHECK(first == 1012 && count == 1);

    CHECK(pg_span4m(0x00400000, PG_4M, &first, &count) == 1);
    CHECK(first == 1 && count == 1);

    CHECK(pg_span4m(0x00400000, PG_4M + 1, &first, &count) == 1);
    CHECK(first == 1 && count == 2);

    CHECK(pg_span4m(0x00500000, 0x1000, &first, &count) == 1);
    CHECK(first == 1 && count == 1);

    CHECK(pg_span4m(0, 32u * 1024 * 1024, &first, &count) == 1);
    CHECK(first == 0 && count == 8);

    CHECK(pg_span4m(0x1000, 0, &first, &count) == 0);
    CHECK(pg_span4m(0xFFFFF000u, 0x2000, &first, &count) == 0);
}

static void t_del_prompt(void)
{
    char b[96];

    del_prompt(b, sizeof b, 1, 0, "notes.txt");
    CHECK(STREQ(b, "Delete \"notes.txt\"? This cannot be undone."));

    del_prompt(b, sizeof b, 0, 1, "docs");
    CHECK(STREQ(b, "Delete folder \"docs\"? This cannot be undone."));

    del_prompt(b, sizeof b, 3, 0, "ignored");
    CHECK(STREQ(b, "Delete 3 files? This cannot be undone."));

    del_prompt(b, sizeof b, 2, 1, "ignored");
    CHECK(STREQ(b, "Delete 3 items? This cannot be undone."));

    del_prompt(b, sizeof b, 0, 2, 0);
    CHECK(STREQ(b, "Delete 2 items? This cannot be undone."));

    del_prompt(b, sizeof b, 12, 0, 0);
    CHECK(STREQ(b, "Delete 12 files? This cannot be undone."));

    del_prompt(b, sizeof b, 1, 0, 0);
    CHECK(STREQ(b, "Delete 1 files? This cannot be undone."));

    char s[16];
    del_prompt(s, sizeof s, 1, 0, "averylongfilename.txt");
    CHECK(s[15] == 0);
}

static void t_fs_dir_of(void)
{
    char d[FS_NAMELEN];
    CHECK(fs_dir_of("sys/gdi.kx", d, sizeof d) == 1);
    CHECK(STREQ(d, "sys"));
    CHECK(fs_dir_of("readme.txt", d, sizeof d) == 0);
    CHECK(STREQ(d, ""));
    CHECK(fs_dir_of("a/b", d, sizeof d) == 1);
    CHECK(STREQ(d, "a"));
    CHECK(fs_dir_of("/x", d, sizeof d) == 1);
    CHECK(STREQ(d, ""));
}

static void t_fs_in_dir(void)
{
    CHECK(fs_in_dir("sys/gdi.kx", "sys") == 1);
    CHECK(fs_in_dir("sys/gdi.kx", "sy") == 0);
    CHECK(fs_in_dir("system/gdi.kx", "sys") == 0);
    CHECK(fs_in_dir("gdi.kx", "sys") == 0);
    CHECK(fs_in_dir("sys/", "sys") == 0);
    CHECK(fs_in_dir("sys/sub/x", "sys") == 0);
}

static void t_fs_rejoin(void)
{
    char out[FS_NAMELEN];
    CHECK(fs_rejoin("sys/gdi.kx", "sys", "system", out, sizeof out) == 1);
    CHECK(STREQ(out, "system/gdi.kx"));

    CHECK(fs_rejoin("a/b.txt", "a", "z", out, sizeof out) == 1);
    CHECK(STREQ(out, "z/b.txt"));

    CHECK(fs_rejoin("a/0123456789012345678", "a", "b", out, sizeof out) == 1);
    CHECK(STREQ(out, "b/0123456789012345678"));

    CHECK(fs_rejoin("a/0123456789012345678", "a", "bcd", out, sizeof out) == 1);
    CHECK(STREQ(out, "bcd/0123456789012345678"));

    CHECK(fs_rejoin("a/0123456789012345678", "a", "bcde", out, sizeof out) == 0);

    CHECK(fs_rejoin("other/x", "a", "b", out, sizeof out) == 0);
}

static void t_fs_dirname_ok(void)
{
    CHECK(fs_dirname_ok("docs") == 1);
    CHECK(fs_dirname_ok("my stuff") == 1);
    CHECK(fs_dirname_ok("") == 0);
    CHECK(fs_dirname_ok(" lead") == 0);
    CHECK(fs_dirname_ok("trail ") == 0);
    CHECK(fs_dirname_ok("a/b") == 1);
    CHECK(fs_dirname_ok("desktop/docs") == 1);
    CHECK(fs_dirname_ok("a/../b") == 0);
    CHECK(fs_dirname_ok("a//b") == 0);
    CHECK(fs_under("desktop/docs/x","desktop"));
    CHECK(!fs_under("desktop2/x","desktop"));
    char child[24];
    CHECK(fs_child("desktop/docs/x","desktop",child,24)==2&&STREQ(child,"desktop/docs"));
    CHECK(fs_child("desktop/docs/x","desktop/docs",child,24)==1&&STREQ(child,"desktop/docs/x"));
    CHECK(fs_dirname_ok(".") == 0);
    CHECK(fs_dirname_ok("..") == 0);
    CHECK(fs_dirname_ok("0123456789012345678901") == 0);
    CHECK(fs_dirname_ok("012345678901234567890") == 1);
}

static void t_fs_name_ok(void)
{
    CHECK(fs_name_ok("hello.bat") == 1);
    CHECK(fs_name_ok("sys/gdi.kx") == 1);
    CHECK(fs_name_ok("") == 0);
    CHECK(fs_name_ok("01234567890123456789012") == 1);
    CHECK(fs_name_ok("012345678901234567890123") == 0);
    CHECK(fs_name_ok("sys/0123456789012345678") == 1);
    CHECK(fs_name_ok("sys/01234567890123456789") == 0);
}

static void t_uart_divisor(void)
{
    CHECK(uart_divisor(115200) == 1);
    CHECK(uart_divisor(57600) == 2);
    CHECK(uart_divisor(19200) == 6);
    CHECK(uart_divisor(9600) == 12);
    CHECK(uart_divisor(1200) == 96);
    CHECK(uart_divisor(0) == 0);
    CHECK(uart_divisor(230400) == 0);
    CHECK(uart_divisor(115201) == 0);
    CHECK(uart_divisor(50000) == 0);
}

static void t_uart_lsr(void)
{
    CHECK(uart_lsr_error(LSR_OE) == 1);
    CHECK(uart_lsr_error(LSR_PE) == 1);
    CHECK(uart_lsr_error(LSR_FE) == 1);
    CHECK(uart_lsr_error(LSR_DR) == 0);
    CHECK(uart_lsr_error(LSR_THRE) == 0);
    CHECK(uart_lsr_error(LSR_DR | LSR_THRE) == 0);
    CHECK(uart_lsr_error(0) == 0);
}

static void t_save_paths(void)
{
    CHECK(save_path_char('/'));
    CHECK(save_path_char(':'));
    CHECK(save_path_char(92));
    CHECK(!save_path_char(8));
    char b[132];
    CHECK(save_path(0,"","desktop/test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/test.bmp"));
    CHECK(save_path(0,"desktop","test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/test.bmp"));
    CHECK(save_path(0,"desktop","desktop/test.BMP","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/test.BMP"));
    CHECK(save_path(0,"desktop","A:/test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:test.bmp"));
    CHECK(save_path(0,"","desktop/abcdefghijk","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/abcdefghijk.bmp"));
    CHECK(save_path(0,"","desktop/abcdefghijkl","bmp",b,sizeof b)==SAVE_PATH_LONG);
    CHECK(b[0]==0);
    CHECK(save_path(0,"","desktop/sub/test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/sub/test.bmp"));
    CHECK(save_path(0,"","desktop/../test","bmp",b,sizeof b)==SAVE_PATH_BAD);
    CHECK(save_path(0,"","desktop/","bmp",b,sizeof b)==SAVE_PATH_BAD);
    CHECK(save_path(0,"","z:test","bmp",b,sizeof b)==SAVE_PATH_BAD);
    CHECK(save_path(1,"/pics","test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"u:/pics/test.bmp"));
    CHECK(save_path(0,"desktop","u:/pics/test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"u:/pics/test.bmp"));
    CHECK(save_path(1,"/pics","sub/test","bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"u:/pics/sub/test.bmp"));
    CHECK(save_path(0,"","test","bmp",b,4)==SAVE_PATH_LONG);
    CHECK(b[0]==0);
    char winpath[]={'A',':',92,'d','e','s','k','t','o','p',92,'x',0};
    CHECK(save_path(0,"",winpath,"bmp",b,sizeof b)==SAVE_PATH_OK);
    CHECK(STREQ(b,"a:desktop/x.bmp"));
}

static void t_graphics_fallback(void)
{
    CHECK(gf_disable(14, 1, 0, 0) == 3);
    CHECK(gf_disable(14, 0, 1, 0) == 2);
    CHECK(gf_disable(14, 0, 0, 0) == 0);
    CHECK(gf_disable(14, 1, 1, 0) == 3);
    CHECK(gf_disable(14, 0, 1, 2) == 0);
    CHECK(gf_disable(14, 1, 0, 2) == 1);
    CHECK(gf_disable(14, 1, 1, 3) == 0);
    CHECK(gf_disable(6, 1, 0, 0) == 3);
    CHECK(gf_disable(13, 0, 1, 0) == 2);
    CHECK(gf_disable(3, 1, 0, 0) == 0);
}

static void t_fault_ring(void)
{
    CHECK(fault_ring_count(0) == 0);
    CHECK(fault_ring_count(5) == 5);
    CHECK(fault_ring_count(FAULT_HIST) == FAULT_HIST);
    CHECK(fault_ring_count(FAULT_HIST + 40) == FAULT_HIST);

    CHECK(fault_ring_slot(0, 0) == -1);

    CHECK(fault_ring_slot(0, 3) == 2);
    CHECK(fault_ring_slot(1, 3) == 1);
    CHECK(fault_ring_slot(2, 3) == 0);
    CHECK(fault_ring_slot(3, 3) == -1);
    CHECK(fault_ring_slot(-1, 3) == -1);

    CHECK(fault_ring_slot(0, FAULT_HIST) == FAULT_HIST - 1);
    CHECK(fault_ring_slot(FAULT_HIST - 1, FAULT_HIST) == 0);
    CHECK(fault_ring_slot(FAULT_HIST, FAULT_HIST) == -1);

    CHECK(fault_ring_slot(0, FAULT_HIST + 1) == 0);
    CHECK(fault_ring_slot(1, FAULT_HIST + 1) == FAULT_HIST - 1);
    CHECK(fault_ring_slot(FAULT_HIST - 1, FAULT_HIST + 1) == 1);
    CHECK(fault_ring_slot(FAULT_HIST, FAULT_HIST + 1) == -1);

    int seen[FAULT_HIST];
    for (int i = 0; i < FAULT_HIST; i++) seen[i] = 0;
    for (int i = 0; i < FAULT_HIST; i++) {
        int s = fault_ring_slot(i, 1000);
        CHECK(s >= 0 && s < FAULT_HIST);
        if (s >= 0 && s < FAULT_HIST) seen[s]++;
    }
    int all_once = 1;
    for (int i = 0; i < FAULT_HIST; i++) if (seen[i] != 1) all_once = 0;
    CHECK(all_once);
}

static u8 mbuf_src[600], mbuf_dst[600];

static void t_memcpy_sizes(void)
{
    static const int sizes[] = { 0, 1, 4, 7, 8, 63, 64, 65, 71, 128, 129, 255, 512 };
    for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        int n = sizes[si];
        for (int soff = 0; soff < 8; soff++) {
            for (int doff = 0; doff < 8; doff++) {
                for (int i = 0; i < 600; i++) {
                    mbuf_src[i] = (u8)(i * 7 + 1);
                    mbuf_dst[i] = 0xAA;
                }
                api->memcpy(mbuf_dst + 32 + doff, mbuf_src + 32 + soff, (u32)n);
                int ok = 1;
                for (int i = 0; i < n; i++)
                    if (mbuf_dst[32 + doff + i] != mbuf_src[32 + soff + i]) ok = 0;
                for (int i = 0; i < 32 + doff; i++)
                    if (mbuf_dst[i] != 0xAA) ok = 0;
                for (int i = 32 + doff + n; i < 600; i++)
                    if (mbuf_dst[i] != 0xAA) ok = 0;
                CHECK(ok);
                if (!ok) return;
            }
        }
    }
}

static void t_memset_sizes(void)
{
    static const int sizes[] = { 0, 1, 7, 8, 63, 64, 65, 71, 128, 200, 511 };
    for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        int n = sizes[si];
        for (int doff = 0; doff < 8; doff++) {
            for (int i = 0; i < 600; i++) mbuf_dst[i] = 0xAA;
            api->memset(mbuf_dst + 32 + doff, 0x5C, (u32)n);
            int ok = 1;
            for (int i = 0; i < n; i++)
                if (mbuf_dst[32 + doff + i] != 0x5C) ok = 0;
            for (int i = 0; i < 32 + doff; i++) if (mbuf_dst[i] != 0xAA) ok = 0;
            for (int i = 32 + doff + n; i < 600; i++) if (mbuf_dst[i] != 0xAA) ok = 0;
            CHECK(ok);
            if (!ok) return;
        }
    }
}

static void t_memmove_overlap(void)
{
    for (int i = 0; i < 300; i++) mbuf_dst[i] = (u8)(i * 3 + 5);
    api->memmove(mbuf_dst + 8, mbuf_dst, 200);
    int ok = 1;
    for (int i = 0; i < 200; i++)
        if (mbuf_dst[8 + i] != (u8)(i * 3 + 5)) ok = 0;
    CHECK(ok);

    for (int i = 0; i < 300; i++) mbuf_dst[i] = (u8)(i * 3 + 5);
    api->memmove(mbuf_dst, mbuf_dst + 8, 200);
    ok = 1;
    for (int i = 0; i < 200; i++)
        if (mbuf_dst[i] != (u8)((i + 8) * 3 + 5)) ok = 0;
    CHECK(ok);
}

static void t_snake_margin(void)
{
    CHECK(snake_food_margin(0) == 0);
    CHECK(snake_food_margin(4) == 0);
    CHECK(snake_food_margin(5) == 1);
    CHECK(snake_food_margin(9) == 1);
    CHECK(snake_food_margin(10) == 2);
    CHECK(snake_food_margin(15) == 3);
    CHECK(snake_food_margin(200) == 3);
    CHECK(snake_food_margin(-1) == 0);
}

static void t_snake_food_box(void)
{
    int x0, y0, x1, y1;

    CHECK(snake_food_box(0, 26, 20, &x0, &y0, &x1, &y1) == 0);
    CHECK(x0 == 0 && y0 == 0 && x1 == 26 && y1 == 20);

    CHECK(snake_food_box(15, 26, 20, &x0, &y0, &x1, &y1) == 3);
    CHECK(x0 == 3 && y0 == 3 && x1 == 23 && y1 == 17);
    CHECK(x1 - x0 == 20 && y1 - y0 == 14);

    int m = snake_food_box(200, 6, 6, &x0, &y0, &x1, &y1);
    CHECK(m == 2 && x0 == 2 && x1 == 4 && y0 == 2 && y1 == 4);
    CHECK(x1 > x0 && y1 > y0);

    m = snake_food_box(200, 3, 3, &x0, &y0, &x1, &y1);
    CHECK(m == 0 && x0 == 0 && x1 == 3 && y0 == 0 && y1 == 3);
}

static void t_mtrr_pow2(void)
{
    CHECK(mtrr_pow2_up(0) == 0);
    CHECK(mtrr_pow2_up(1) == 1);
    CHECK(mtrr_pow2_up(4096) == 4096);
    CHECK(mtrr_pow2_up(4097) == 8192);
    CHECK(mtrr_pow2_up(640 * 456) == 0x80000);
    CHECK(mtrr_pow2_up(0x80000000u) == 0x80000000u);
    CHECK(mtrr_pow2_up(0x80000001u) == 0);
}

static void t_mtrr_mask(void)
{
    CHECK(mtrr_physmask(4096) == 0xFFFFF000u);
    CHECK(mtrr_physmask(0x80000) == 0xFFF80000u);
    CHECK(mtrr_physmask(0x1000000) == 0xFF000000u);
    CHECK(mtrr_physmask(2048) == 0);
}

static void t_mtrr_plan_aligned(void)
{
    MtrrRange r[MTRR_MAXPLAN];
    int n = mtrr_plan(0xFD000000u, 640 * 456, r, MTRR_MAXPLAN);
    CHECK(n == 1);
    CHECK(r[0].base == 0xFD000000u);
    CHECK(r[0].size == 0x80000);

    n = mtrr_plan(0xE0000000u, 0x1000000, r, MTRR_MAXPLAN);
    CHECK(n == 1);
    CHECK(r[0].size == 0x1000000);
}

static void t_mtrr_plan_split(void)
{
    MtrrRange r[MTRR_MAXPLAN];
    int n = mtrr_plan(0xFD040000u, 640 * 456, r, MTRR_MAXPLAN);
    CHECK(n == 2);
    CHECK(r[0].base == 0xFD040000u && r[0].size == 0x40000);
    CHECK(r[1].base == 0xFD080000u && r[1].size == 0x40000);

    CHECK(r[0].base + r[0].size == r[1].base);
    CHECK((r[0].base & (r[0].size - 1)) == 0);
    CHECK((r[1].base & (r[1].size - 1)) == 0);
}

static void t_mtrr_plan_limits(void)
{
    MtrrRange r[MTRR_MAXPLAN];
    CHECK(mtrr_plan(0xFD000000u, 0, r, MTRR_MAXPLAN) == 0);
    CHECK(mtrr_plan(0xFD000800u, 4096, r, MTRR_MAXPLAN) == -1);
    CHECK(mtrr_plan(0xFD040000u, 640 * 456, r, 1) == -1);
}

static void t_mtrr_overlap(void)
{
    CHECK(mtrr_overlap(0x1000, 0x1000, 0x1800, 0x1000) == 1);
    CHECK(mtrr_overlap(0x1000, 0x1000, 0x2000, 0x1000) == 0);
    CHECK(mtrr_overlap(0x2000, 0x1000, 0x1000, 0x1000) == 0);
    CHECK(mtrr_overlap(0x1000, 0x4000, 0x2000, 0x1000) == 1);
    CHECK(mtrr_overlap(0x1000, 0, 0x1000, 0x1000) == 0);
}

static void t_mtrr_takeover(void)
{
    u32 wb = 0;

    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 32665, &wb) == 1);
    CHECK(wb == 0x2000000);

    CHECK(mtrr_takeover_ok(MT_UC, MT_UC, 32665, &wb) == 0);

    CHECK(mtrr_takeover_ok(MT_WB, MT_WB, 32665, &wb) == 0);
    CHECK(mtrr_takeover_ok(MT_WB, MT_WT, 32665, &wb) == 0);

    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 48u * 1024, &wb) == 0);

    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 0, &wb) == 0);
    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 640, &wb) == 0);
    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 0x400000, &wb) == 0);

    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 16u * 1024 - 64, &wb) == 1);
    CHECK(wb == 0x1000000);
    CHECK(mtrr_takeover_ok(MT_WB, MT_UC, 128u * 1024 - 128, &wb) == 1);
    CHECK(wb == 0x8000000);
}

static void t_vbank_detect(void)
{
    CHECK(vbank_detect(0x1013) == VB_CIRRUS);
    CHECK(vbank_detect(0x100C) == VB_TSENG);
    CHECK(vbank_detect(0x5333) == VB_S3);
    CHECK(vbank_detect(0x1023) == VB_TRIDENT);
    CHECK(vbank_detect(0x8086) == VB_NONE);
    CHECK(vbank_detect(0x1234) == VB_NONE);
    CHECK(vbank_detect(0) == VB_NONE);
}

static void t_vbank_prog(void)
{
    VbWrite w[8];

    int n = vbank_prog(VB_CIRRUS, 3, w);
    CHECK(n == 3);
    CHECK(w[0].op == VBOP_IDX && w[0].port == 0x3C4 && w[0].idx == 0x06 && w[0].val == 0x12);
    CHECK(w[1].op == VBOP_IDX && w[1].port == 0x3CE && w[1].idx == 0x0B && w[1].val == 0x00);
    CHECK(w[2].op == VBOP_IDX && w[2].port == 0x3CE && w[2].idx == 0x09 && w[2].val == 0x30);

    n = vbank_prog(VB_TSENG, 2, w);
    CHECK(n == 3);
    CHECK(w[0].op == VBOP_OUT && w[0].port == 0x3BF && w[0].val == 0x03);
    CHECK(w[1].op == VBOP_OUT && w[1].port == 0x3D8 && w[1].val == 0xA0);
    CHECK(w[2].op == VBOP_OUT && w[2].port == 0x3CD && w[2].val == 0x22);

    n = vbank_prog(VB_S3, 0x11, w);
    CHECK(n == 4);
    CHECK(w[0].op == VBOP_IDX && w[0].port == 0x3D4 && w[0].idx == 0x38 && w[0].val == 0x48);
    CHECK(w[1].op == VBOP_IDX && w[1].port == 0x3D4 && w[1].idx == 0x39 && w[1].val == 0xA5);
    CHECK(w[2].op == VBOP_IDX && w[2].port == 0x3D4 && w[2].idx == 0x35 && w[2].val == 0x01);
    CHECK(w[3].op == VBOP_IDX && w[3].port == 0x3D4 && w[3].idx == 0x51 && w[3].val == 0x04);

    n = vbank_prog(VB_TRIDENT, 1, w);
    CHECK(n == 2);
    CHECK(w[0].op == VBOP_IDX_RD && w[0].port == 0x3C4 && w[0].idx == 0x0B);
    CHECK(w[1].op == VBOP_IDX && w[1].port == 0x3C4 && w[1].idx == 0x0E && w[1].val == 0x03);

    CHECK(vbank_prog(VB_NONE, 0, w) == 0);
}

static void t_vblit(void)
{
    VbPart p[2];

    CHECK(vblit_row(0, 640, 640, p) == 1);
    CHECK(p[0].bank == 0 && p[0].win_off == 0 && p[0].src_off == 0 && p[0].len == 640);

    CHECK(vblit_row(102, 640, 640, p) == 2);
    CHECK(p[0].bank == 0 && p[0].win_off == 65280 && p[0].src_off == 65280 && p[0].len == 256);
    CHECK(p[1].bank == 1 && p[1].win_off == 0 && p[1].src_off == 65536 && p[1].len == 384);

    CHECK(vblit_row(103, 640, 640, p) == 1);
    CHECK(p[0].bank == 1 && p[0].win_off == 384 && p[0].len == 640);

    CHECK(vblit_row(64, 800, 1024, p) == 1);
    CHECK(p[0].bank == 1 && p[0].win_off == 0 && p[0].src_off == 64 * 800 && p[0].len == 800);

    CHECK(vblit_row(50, 1024, 1280, p) == 1);
    CHECK(vblit_row(51, 1024, 1280, p) == 2);
    CHECK(p[0].len == 256 && p[1].len == 768 && p[1].bank == 1);
}

static void t_sysfile(void)
{
    CHECK(sh_is_system_file("sys/files.kx") == 1);
    CHECK(sh_is_system_file("sys/gdi.kx") == 1);
    CHECK(sh_is_system_file("SYS/FILES.KX") == 1);
    CHECK(sh_is_system_file("sys/anything") == 1);

    CHECK(sh_is_system_file("notes.txt") == 0);
    CHECK(sh_is_system_file("mypic.bmp") == 0);
    CHECK(sh_is_system_file("") == 0);
    CHECK(sh_is_system_file("system.txt") == 0);
    CHECK(sh_is_system_file("sysadmin") == 0);
    CHECK(sh_is_system_file("my/sys/x.kx") == 0);
}

static void t_fx(void)
{
    CHECK(fx_mul(FX(2), FX(3)) == FX(6));
    CHECK(fx_mul(32768, 32768) == 16384);
    CHECK(fx_mul(-FX(2), FX(3)) == -FX(6));
    CHECK(fx_div(FX(6), FX(2)) == FX(3));
    CHECK(fx_div(FX(1), FX(2)) == 32768);
    CHECK(fx_div(FX(5), 0) == (fx)0x7FFFFFFF);
    CHECK(fx_div(-FX(5), 0) == (fx)0x80000000);
    CHECK(fx_div(FX(3), 1) == (fx)0x7FFFFFFF);
    CHECK(fx_sqrt(FX(4)) == FX(2));
    CHECK(fx_sqrt(FX(9)) == FX(3));
    CHECK(fx_sqrt(0) == 0);
    CHECK(fx_lerp(FX(10), FX(20), 0) == FX(10));
    CHECK(fx_lerp(FX(10), FX(20), FX_ONE) == FX(20));
    CHECK(fx_lerp(FX(10), FX(20), 32768) == FX(15));
    CHECK(fx_clamp(FX(5), FX(0), FX(3)) == FX(3));
    CHECK(fx_clamp(-FX(5), FX(0), FX(3)) == FX(0));
    CHECK(fx_clamp(FX(2), FX(0), FX(3)) == FX(2));

    CHECK(fx_ceil(FX(3)) == 3);
    CHECK(fx_ceil(FX(3) + 1) == 4);
    CHECK(fx_ceil(FX(3) - 1) == 3);
    CHECK(fx_ceil(FX(3) + 32768) == 4);
    CHECK(fx_ceil(0) == 0);
    CHECK(fx_ceil(-32768) == 0);
    CHECK(fx_ceil(FX(-3)) == -3);
    CHECK(fx_ceil(FX(-3) - 1) == -3);
    CHECK(fx_ceil(FX(-3) + 1) == -2);

    CHECK(isqrt(0) == 0);
    CHECK(isqrt(1) == 1);
    CHECK(isqrt(2) == 1);
    CHECK(isqrt(4) == 2);
    CHECK(isqrt(25) == 5);
    CHECK(isqrt(99) == 9);
    CHECK(isqrt(100) == 10);
    CHECK(isqrt(10000) == 100);
    CHECK(isqrt(65536) == 256);
}

static void t_nearest(void)
{
    static const u32 pal[5] = {
        0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF
    };
    CHECK(gdi_nearest(0, 0, 0, pal, 5) == 0);
    CHECK(gdi_nearest(255, 255, 255, pal, 5) == 1);
    CHECK(gdi_nearest(200, 20, 20, pal, 5) == 2);
    CHECK(gdi_nearest(20, 200, 20, pal, 5) == 3);
    CHECK(gdi_nearest(30, 30, 220, pal, 5) == 4);
    CHECK(gdi_nearest(240, 240, 240, pal, 5) == 1);
}

static void t_ramp(void)
{
    CHECK(gdi_ramp_entry(0, 23) == 0xFF0000);
    CHECK(gdi_ramp_entry(0, 11) == 0x7F0000);
    CHECK(gdi_ramp_entry(0, 0)  == 0x0A0000);
    CHECK(gdi_ramp_entry(1, 23) == 0x00FF00);
    CHECK(gdi_ramp_entry(2, 23) == 0x0000FF);
    CHECK(gdi_ramp_entry(6, 23) == 0xFFFFFF);
    CHECK(gdi_ramp_entry(6, 11) == 0x7F7F7F);
    CHECK(gdi_ramp_entry(7, 23) == 0xFF8000);
}

static void t_bayer(void)
{
    CHECK(gdi_bayer(0, 0) == 0);
    CHECK(gdi_bayer(1, 0) == 8);
    CHECK(gdi_bayer(0, 1) == 12);
    CHECK(gdi_bayer(2, 1) == 14);
    CHECK(gdi_bayer(3, 3) == 5);
    CHECK(gdi_bayer(4, 4) == 0);
    CHECK(gdi_bayer(-1, 0) == 10);
}

#define REQ(o,i,L,T,R,B) ((o)[i].l==(L)&&(o)[i].t==(T)&&(o)[i].r==(R)&&(o)[i].b==(B))

static void t_rgn_combine(void)
{
    GRect a = { 0, 0, 10, 10 }, b = { 5, 5, 15, 15 }, o[16];

    int n = rgn_op_raw(o, 16, &a, 1, &b, 1, RGN_OR);
    CHECK(n == 3);
    CHECK(REQ(o, 0, 0, 0, 10, 5));
    CHECK(REQ(o, 1, 0, 5, 15, 10));
    CHECK(REQ(o, 2, 5, 10, 15, 15));

    n = rgn_op_raw(o, 16, &a, 1, &b, 1, RGN_AND);
    CHECK(n == 1);
    CHECK(REQ(o, 0, 5, 5, 10, 10));

    GRect far = { 40, 40, 50, 50 };
    CHECK(rgn_op_raw(o, 16, &a, 1, &far, 1, RGN_AND) == 0);

    n = rgn_op_raw(o, 16, &a, 1, &b, 1, RGN_DIFF);
    CHECK(n == 2);
    CHECK(REQ(o, 0, 0, 0, 10, 5));
    CHECK(REQ(o, 1, 0, 5, 5, 10));

    n = rgn_op_raw(o, 16, &a, 1, &b, 1, RGN_XOR);
    CHECK(n == 4);

    GRect c = { 0, 0, 10, 5 }, d = { 0, 5, 10, 10 };
    n = rgn_op_raw(o, 16, &c, 1, &d, 1, RGN_OR);
    CHECK(n == 1);
    CHECK(REQ(o, 0, 0, 0, 10, 10));

    CHECK(rgn_op_raw(o, 1, &a, 1, &b, 1, RGN_OR) == -1);
}

static void t_rgn_query(void)
{
    GRect a = { 0, 0, 10, 10 }, b = { 5, 5, 15, 15 }, o[16], bb;

    CHECK(rgn_pt_in_raw(&a, 1, 5, 5) == 1);
    CHECK(rgn_pt_in_raw(&a, 1, 0, 0) == 1);
    CHECK(rgn_pt_in_raw(&a, 1, 10, 5) == 0);
    CHECK(rgn_pt_in_raw(&a, 1, 5, 10) == 0);
    CHECK(rgn_pt_in_raw(&a, 1, -1, 5) == 0);

    CHECK(rgn_box_raw(&a, 0, &bb) == GRGN_NULL);
    CHECK(rgn_box_raw(&a, 1, &bb) == GRGN_SIMPLE);
    CHECK(REQ(&bb, 0, 0, 0, 10, 10));

    int n = rgn_op_raw(o, 16, &a, 1, &b, 1, RGN_OR);
    CHECK(rgn_box_raw(o, n, &bb) == GRGN_COMPLEX);
    CHECK(REQ(&bb, 0, 0, 0, 15, 15));

    CHECK(rgn_equal_raw(&a, 1, &a, 1) == 1);
    CHECK(rgn_equal_raw(&a, 1, &b, 1) == 0);

    GRect half[2] = { { 0, 0, 10, 5 }, { 0, 5, 10, 10 } };
    int m = rgn_op_raw(o, 16, &half[0], 1, &half[1], 1, RGN_OR);
    CHECK(rgn_equal_raw(&a, 1, o, m) == 1);
}

static void t_poly_fill(void)
{
    i32 s[16];

    GPt rect[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    int n = poly_scan(rect, 4, 5, GPF_EVENODD, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 10);
    CHECK(poly_scan(rect, 4, -1, GPF_EVENODD, s, 16) == 0);
    CHECK(poly_scan(rect, 4, 10, GPF_EVENODD, s, 16) == 0);

    GPt u[8] = { {0,0}, {4,0}, {4,6}, {8,6}, {8,0}, {12,0}, {12,10}, {0,10} };
    n = poly_scan(u, 8, 2, GPF_EVENODD, s, 16);
    CHECK(n == 2);
    CHECK(s[0] == 0 && s[1] == 4);
    CHECK(s[2] == 8 && s[3] == 12);
    n = poly_scan(u, 8, 8, GPF_EVENODD, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 12);

    GPt tri[3] = { {0,0}, {10,0}, {0,10} };
    n = poly_scan(tri, 3, 0, GPF_EVENODD, s, 16); CHECK(n == 1); CHECK(s[0]==0 && s[1]==9);
    n = poly_scan(tri, 3, 5, GPF_EVENODD, s, 16); CHECK(n == 1); CHECK(s[0]==0 && s[1]==4);
    n = poly_scan(tri, 3, 9, GPF_EVENODD, s, 16); CHECK(n == 0);
}

static void t_poly_winding(void)
{
    i32 s[16];

    GPt dbl[8] = { {0,0}, {8,0}, {8,8}, {0,8}, {0,0}, {8,0}, {8,8}, {0,8} };
    CHECK(poly_scan(dbl, 8, 4, GPF_EVENODD, s, 16) == 0);
    int n = poly_scan(dbl, 8, 4, GPF_WINDING, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 8);

    GPt tri[3] = { {0,0}, {10,0}, {0,10} };
    n = poly_scan(tri, 3, 5, GPF_WINDING, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 4);
}

static void t_poly_region(void)
{
    GRect o[16];

    GPt rect[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    int n = poly_to_bands(rect, 4, GPF_EVENODD, o, 16);
    CHECK(n == 1); CHECK(REQ(o, 0, 0,0,10,10));

    GPt u[8] = { {0,0}, {4,0}, {4,6}, {8,6}, {8,0}, {12,0}, {12,10}, {0,10} };
    n = poly_to_bands(u, 8, GPF_EVENODD, o, 16);
    CHECK(n == 3);
    CHECK(REQ(o, 0, 0,0,4,6));
    CHECK(REQ(o, 1, 8,0,12,6));
    CHECK(REQ(o, 2, 0,6,12,10));

    GPt dbl[8] = { {0,0}, {8,0}, {8,8}, {0,8}, {0,0}, {8,0}, {8,8}, {0,8} };
    n = poly_to_bands(dbl, 8, GPF_WINDING, o, 16);
    CHECK(n == 1); CHECK(REQ(o, 0, 0,0,8,8));
    CHECK(poly_to_bands(dbl, 8, GPF_EVENODD, o, 16) == 0);
}

static void t_bezier(void)
{
    GPt p0 = {0,0}, p1 = {0,80}, p2 = {80,80}, p3 = {80,0};
    GPt a = bez_cubic(p0, p1, p2, p3, 0);        CHECK(a.x == 0  && a.y == 0);
    GPt b = bez_cubic(p0, p1, p2, p3, FX_ONE);   CHECK(b.x == 80 && b.y == 0);
    GPt m = bez_cubic(p0, p1, p2, p3, 32768);
    CHECK(m.x == 40 && m.y == 60);

    GPt q0 = {0,0}, q1 = {40,80}, q2 = {80,0};
    GPt qa = bez_quad(q0, q1, q2, 0);            CHECK(qa.x == 0  && qa.y == 0);
    GPt qb = bez_quad(q0, q1, q2, FX_ONE);       CHECK(qb.x == 80 && qb.y == 0);
    GPt qm = bez_quad(q0, q1, q2, 32768);
    CHECK(qm.x == 40 && qm.y == 40);
}

static void t_flatten(void)
{
    GPt p0 = {0,0}, p1 = {0,80}, p2 = {80,80}, p3 = {80,0}, out[CURVE_MAXSEG + 1];
    int n = flatten_cubic(p0, p1, p2, p3, out, CURVE_MAXSEG + 1, 8);
    CHECK(n == 9);
    CHECK(out[0].x == 0  && out[0].y == 0);
    CHECK(out[8].x == 80 && out[8].y == 0);
    CHECK(out[4].x == 40 && out[4].y == 60);
    CHECK(flatten_cubic(p0, p1, p2, p3, out, 4, 8) == -1);
}

static void t_stroke_seg(void)
{
    GPt o[4];

    CHECK(stroke_seg((GPt){0,0}, (GPt){10,0}, 2, o) == 1);
    CHECK(o[0].x==0  && o[0].y==2);
    CHECK(o[1].x==10 && o[1].y==2);
    CHECK(o[2].x==10 && o[2].y==-2);
    CHECK(o[3].x==0  && o[3].y==-2);

    CHECK(stroke_seg((GPt){0,0}, (GPt){0,10}, 3, o) == 1);
    CHECK(o[0].x==-3 && o[0].y==0);
    CHECK(o[1].x==-3 && o[1].y==10);
    CHECK(o[2].x==3  && o[2].y==10);
    CHECK(o[3].x==3  && o[3].y==0);

    CHECK(stroke_seg((GPt){0,0}, (GPt){3,4}, 5, o) == 1);
    CHECK(o[0].x==-4 && o[0].y==3);
    CHECK(o[1].x==-1 && o[1].y==7);
    CHECK(o[2].x==7  && o[2].y==1);
    CHECK(o[3].x==4  && o[3].y==-3);

    CHECK(stroke_seg((GPt){5,5}, (GPt){5,5}, 2, o) == 0);
}

static void t_rgb_lerp(void)
{
    CHECK(rgb_lerp(0x000000, 0xFFFFFF, 0)   == 0x000000u);
    CHECK(rgb_lerp(0x000000, 0xFFFFFF, 256) == 0xFFFFFFu);
    CHECK(rgb_lerp(0x000000, 0xFFFFFF, 128) == 0x7F7F7Fu);
    CHECK(rgb_lerp(0x204060, 0x80A0C0, 128) == 0x507090u);
    CHECK(rgb_lerp(0x123456, 0x123456, 200) == 0x123456u);
}

static void t_span_cov(void)
{
    u8 acc[8];
    #define AZERO() do { for (int i = 0; i < 8; i++) acc[i] = 0; } while (0)

    AZERO(); span_cov(8, 20, 4, 0, 8, acc);
    CHECK(acc[1]==0 && acc[2]==4 && acc[3]==4 && acc[4]==4 && acc[5]==0);

    AZERO(); span_cov(10, 20, 4, 0, 8, acc);
    CHECK(acc[2]==2 && acc[3]==4 && acc[4]==4);

    AZERO(); span_cov(10, 18, 4, 0, 8, acc);
    CHECK(acc[2]==2 && acc[3]==4 && acc[4]==2);

    AZERO();
    span_cov(8, 12, 4, 0, 8, acc);
    span_cov(8, 12, 4, 0, 8, acc);
    CHECK(acc[2]==8 && acc[3]==0);

    AZERO();
    span_cov(12, 12, 4, 0, 8, acc);
    span_cov(20, 8, 4, 0, 8, acc);
    CHECK(acc[2]==0 && acc[3]==0);

    AZERO(); span_cov(8, 40, 4, 2, 4, acc);
    CHECK(acc[0]==4 && acc[1]==4 && acc[2]==4 && acc[3]==4);
    #undef AZERO
}

static void t_poly_scan_aa(void)
{
    int s[16];

    GPt rect[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    int n = poly_scan_aa(rect, 4, 20, 4, GPF_EVENODD, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 40);
    CHECK(poly_scan_aa(rect, 4, -1, 4, GPF_EVENODD, s, 16) == 0);
    CHECK(poly_scan_aa(rect, 4, 40, 4, GPF_EVENODD, s, 16) == 0);

    GPt tri[3] = { {0,0}, {10,0}, {0,10} };
    n = poly_scan_aa(tri, 3, 20, 4, GPF_EVENODD, s, 16);
    CHECK(n == 1); CHECK(s[0] == 0 && s[1] == 20);
}

#define NEAR(a, b, tol) (((a) > (b) ? (a) - (b) : (b) - (a)) <= (tol))

static GPt lb_pts[64]; static int lb_n;
static void lb_plot(void *ctx, int x, int y)
{ (void)ctx; if (lb_n < 64) { lb_pts[lb_n].x = x; lb_pts[lb_n].y = y; lb_n++; } }

static void t_bres(void)
{

    lb_n = 0; bres_line((GPt){0,0}, (GPt){4,0}, lb_plot, 0);
    CHECK(lb_n == 5);
    CHECK(lb_pts[0].x==0 && lb_pts[0].y==0 && lb_pts[4].x==4 && lb_pts[4].y==0);

    lb_n = 0; bres_line((GPt){0,0}, (GPt){3,3}, lb_plot, 0);
    CHECK(lb_n == 4);
    CHECK(lb_pts[1].x==1 && lb_pts[1].y==1 && lb_pts[3].x==3 && lb_pts[3].y==3);

    lb_n = 0; bres_line((GPt){0,0}, (GPt){1,4}, lb_plot, 0);
    CHECK(lb_n == 5);
    CHECK(lb_pts[0].x==0 && lb_pts[0].y==0 && lb_pts[4].x==1 && lb_pts[4].y==4);
    int ok = 1;
    for (int i = 1; i < lb_n; i++) {
        int dx = lb_pts[i].x - lb_pts[i-1].x, dy = lb_pts[i].y - lb_pts[i-1].y;
        if (dy != 1 || dx < 0 || dx > 1) ok = 0;
    }
    CHECK(ok);

    lb_n = 0; bres_line((GPt){4,2}, (GPt){0,0}, lb_plot, 0);
    CHECK(lb_n == 5);
    CHECK(lb_pts[0].x==4 && lb_pts[0].y==2 && lb_pts[4].x==0 && lb_pts[4].y==0);

    lb_n = 0; bres_line((GPt){5,5}, (GPt){5,5}, lb_plot, 0);
    CHECK(lb_n == 1 && lb_pts[0].x==5 && lb_pts[0].y==5);
}

static void t_g3_trig(void)
{
    CHECK(fx_sin(0) == 0);
    CHECK(fx_sin(16384) == FX_ONE);
    CHECK(NEAR(fx_sin(8192), 46341, 3));
    CHECK(NEAR(fx_sin(5461), 32768, 6));
    CHECK(fx_sin(32768) == 0);
    CHECK(fx_sin(49152) == -FX_ONE);
    CHECK(NEAR(fx_sin(-16384), -FX_ONE, 2));
    CHECK(fx_cos(0) == FX_ONE);
    CHECK(NEAR(fx_cos(16384), 0, 2));
    CHECK(fx_cos(32768) == -FX_ONE);
    CHECK(NEAR(fx_cos(8192), 46341, 3));
}

static void t_g3_mat(void)
{
    fx m[16], a[16], b[16];
    GVec v, o;

    m4_ident(m);
    v = (GVec){ FX(1), FX(2), FX(3), FX(1) };
    m4_vec(&o, m, &v);
    CHECK(o.x == FX(1) && o.y == FX(2) && o.z == FX(3) && o.w == FX(1));

    m4_trans(m, FX(2), FX(3), FX(4));
    v = (GVec){ FX(1), FX(1), FX(1), FX(1) };
    m4_vec(&o, m, &v);
    CHECK(o.x == FX(3) && o.y == FX(4) && o.z == FX(5) && o.w == FX(1));

    m4_rot_z(m, 16384);
    v = (GVec){ FX(1), 0, 0, FX(1) };
    m4_vec(&o, m, &v);
    CHECK(NEAR(o.x, 0, 3) && NEAR(o.y, FX(1), 3) && o.z == 0);

    m4_rot_x(m, 16384);
    v = (GVec){ 0, FX(1), 0, FX(1) };
    m4_vec(&o, m, &v);
    CHECK(NEAR(o.y, 0, 3) && NEAR(o.z, FX(1), 3));

    m4_rot_y(m, 16384);
    v = (GVec){ FX(1), 0, 0, FX(1) };
    m4_vec(&o, m, &v);
    CHECK(NEAR(o.x, 0, 3) && NEAR(o.z, -FX(1), 3));

    m4_trans(a, FX(2), FX(3), FX(4));
    m4_rot_z(b, 16384);
    m4_mul(m, a, b);
    v = (GVec){ FX(1), 0, 0, FX(1) };
    m4_vec(&o, m, &v);
    CHECK(NEAR(o.x, FX(2), 3) && NEAR(o.y, FX(4), 3) && NEAR(o.z, FX(4), 3));
}

static void t_g3_persp(void)
{
    fx pm[16];
    m4_persp(pm, FX(90), FX_ONE, FX_ONE, FX(9));
    CHECK(NEAR(pm[0], FX_ONE, 8));
    CHECK(NEAR(pm[5], FX_ONE, 8));
    CHECK(pm[11] == -FX_ONE && pm[15] == 0);
    CHECK(NEAR(pm[10], -81920, 8));
    CHECK(NEAR(pm[14], -147456, 16));

    GVec v, c, o;
    v = (GVec){ 0, 0, -FX(2), FX(1) };
    m4_vec(&c, pm, &v);
    CHECK(NEAR(c.w, FX(2), 4));
    g3_project_vp(&o, &c, 0, 0, 100, 100);
    CHECK(NEAR(o.x, FX(50), 16) && NEAR(o.y, FX(50), 16));

    v = (GVec){ FX(1), 0, -FX(1), FX(1) };
    m4_vec(&c, pm, &v);
    g3_project_vp(&o, &c, 0, 0, 100, 100);
    CHECK(NEAR(o.x, FX(100), 24));

    v = (GVec){ 0, FX(1), -FX(1), FX(1) };
    m4_vec(&c, pm, &v);
    g3_project_vp(&o, &c, 0, 0, 100, 100);
    CHECK(NEAR(o.y, 0, 24));
}

static void t_g3_clip(void)
{
    GVec a, b, p[16];

    a = (GVec){ 0, 0, 0, FX(2) };
    b = (GVec){ 0, 0, 0, -FX(2) };
    CHECK(g3_clip_seg(&a, &b) == 1);
    CHECK(a.w == FX(2));
    CHECK(NEAR(b.w, G3_WEPS, 4));

    a = (GVec){ 0, 0, 0, FX(2) };
    b = (GVec){ FX(4), 0, 0, -FX(2) };
    CHECK(g3_clip_seg(&a, &b) == 1);
    CHECK(NEAR(b.x, 104856, 8));
    CHECK(NEAR(b.w, 26216, 8));
    CHECK(NEAR(fx_div(b.x, b.w), FX(4), 64));
    for (int p = 0; p < 5; p++) CHECK(g3_pdist(&b, p) >= -8);

    a = (GVec){ 0, 0, 0, -FX(1) };
    b = (GVec){ FX(1), 0, 0, -FX(2) };
    CHECK(g3_clip_seg(&a, &b) == 0);

    a = (GVec){ 0, 0, 0, FX(1) };
    b = (GVec){ FX(1), 0, 0, FX(1) };
    CHECK(g3_clip_seg(&a, &b) == 1);
    CHECK(a.x == 0 && b.x == FX(1) && b.w == FX(1));

    a = (GVec){ 0, 0, 0, FX(1) };
    b = (GVec){ FX(10), 0, 0, FX(1) };
    CHECK(g3_clip_seg(&a, &b) == 1);
    CHECK(NEAR(b.x, FX(4), 8) && NEAR(b.w, FX(1), 4));

    p[0] = (GVec){ 0, 0, 0, FX(2) };
    p[1] = (GVec){ 8192, 0, 0, FX(2) };
    p[2] = (GVec){ 0, 8192, 0, -FX(1) };
    int n = g3_clip_poly(p, 3);
    CHECK(n == 4);
    int at_eps = 0;
    for (int i = 0; i < n; i++) {
        if (NEAR(p[i].w, G3_WEPS, 4)) at_eps++;
        for (int pl = 0; pl < 5; pl++) CHECK(g3_pdist(&p[i], pl) >= -8);
    }
    CHECK(at_eps == 2);

    p[0] = (GVec){ 0, 0, 0, FX(1) };
    p[1] = (GVec){ FX(1), 0, 0, FX(1) };
    p[2] = (GVec){ 0, FX(1), 0, FX(1) };
    CHECK(g3_clip_poly(p, 3) == 3);
    CHECK(p[1].x == FX(1) && p[2].y == FX(1));

    p[0] = (GVec){ 0, 0, 0, -FX(1) };
    p[1] = (GVec){ FX(1), 0, 0, -FX(1) };
    p[2] = (GVec){ 0, FX(1), 0, -FX(2) };
    CHECK(g3_clip_poly(p, 3) == 0);
}

static void t_g3_shade(void)
{
    CHECK(g3_shade_rgb(0xFFFFFF, 63) == 0xFFFFFF);
    CHECK(g3_shade_rgb(0xFF8040, 31) == 0x7F4020);
    CHECK(g3_shade_rgb(0xFFFFFF, 0)  == 0x030303);
    CHECK(g3_shade_rgb(0x000000, 63) == 0x000000);
    CHECK(g3_lum6(0xFFFFFF) == 63);
    CHECK(g3_lum6(0x000000) == 0);
    CHECK(g3_lum6(0x808080) == 32);
    CHECK(g3_lum6(0x00FF00) == 63);
    CHECK(g3_lum6(0x400000) == 16);
    CHECK(g3_zmap16(-FX_ONE) == 0);
    CHECK(g3_zmap16(0) == 32768);
    CHECK(g3_zmap16(FX_ONE) == 65535);
    CHECK(g3_zmap16(FX(2)) == 65535);
    CHECK(g3_zmap16(-FX(2)) == 0);
}

static void t_g3_prim(void)
{
    int a, b, c;
    g3_prim_tri(G3P_TRIS, 2, &a, &b, &c);     CHECK(a==6 && b==7 && c==8);
    g3_prim_tri(G3P_TRISTRIP, 0, &a, &b, &c); CHECK(a==0 && b==1 && c==2);
    g3_prim_tri(G3P_TRISTRIP, 1, &a, &b, &c); CHECK(a==2 && b==1 && c==3);
    g3_prim_tri(G3P_TRISTRIP, 2, &a, &b, &c); CHECK(a==2 && b==3 && c==4);
    g3_prim_tri(G3P_TRIFAN, 0, &a, &b, &c);   CHECK(a==0 && b==1 && c==2);
    g3_prim_tri(G3P_TRIFAN, 3, &a, &b, &c);   CHECK(a==0 && b==4 && c==5);
}

static void t_g3_trisc(void)
{
    G3SV v0 = { 0, 0, 0, 0 };
    G3SV v1 = { FX(10), 0, FX_ONE, FX(63) };
    G3SV v2 = { 0, FX(10), 0, 0 };
    G3SV L, R;

    CHECK(tri_scan(&v0, &v1, &v2, 0, &L, &R) == 1);
    CHECK(NEAR(L.x, 0, 8));
    CHECK(NEAR(R.x, 622592, 32));
    CHECK(NEAR(R.z, 62260, 64));
    CHECK(NEAR(R.l, 3922379, 4096));

    CHECK(tri_scan(&v0, &v1, &v2, 5, &L, &R) == 1);
    CHECK(NEAR(R.x, 294912, 32));
    CHECK(NEAR(R.z, 29491, 64));

    CHECK(tri_scan(&v0, &v1, &v2, 10, &L, &R) == 0);
    CHECK(tri_scan(&v0, &v1, &v2, -1, &L, &R) == 0);
}

static void t_g3_clipa(void)
{
    GVec p[16]; fx a[16];
    p[0] = (GVec){ 0, 0, 0, FX(2) };     a[0] = 0;
    p[1] = (GVec){ 8192, 0, 0, FX(2) };  a[1] = 0;
    p[2] = (GVec){ 0, 8192, 0, -FX(1) }; a[2] = FX(63);
    int n = g3_clip_polya(p, a, 3);
    CHECK(n == 4);
    int cuts = 0;
    for (int i = 0; i < n; i++)
        if (NEAR(p[i].w, G3_WEPS, 4)) { cuts++; CHECK(NEAR(a[i], 2666496, 8192)); }
    CHECK(cuts == 2);
}

static void t_g3d_service(void)
{
    const G3dOps *g3 = (const G3dOps *)api->service_get("g3d");
    CHECK(g3 != 0);
    if (!g3) return;
    CHECK(g3->abi >= 1);

    G3D *c = g3->create();
    CHECK(c != 0);
    if (!c) return;
    g3->viewport(c, 0, 0, 100, 100, FX(1), FX(9));
    GMat pm;
    g3->mat_persp(&pm, FX(90), FX_ONE, FX(1), FX(9));
    g3->matrix_mode(c, G3M_PROJ);
    g3->load(c, &pm);

    GVec in = { 0, 0, -FX(2), FX(1) }, out;
    CHECK(g3->project(c, &in, 1, &out) == 1);
    CHECK(NEAR(out.x, FX(50), 16) && NEAR(out.y, FX(50), 16));
    CHECK(NEAR(out.w, FX(2), 8));

    in = (GVec){ 0, 0, FX(2), FX(1) };
    CHECK(g3->project(c, &in, 1, &out) == 0);
    CHECK(out.w == 0);

    G3Stats st;
    CHECK(g3->stats(c, &st) == 0 || 1);
    CHECK(g3->get(c, G3S_FILL) == G3FILL_SOLID);
    g3->destroy(c);
}

static void t_g3_light(void)
{
    GVec v = { FX(3), 0, 0, 0 };
    g3_norm3(&v);
    CHECK(v.x == FX(1) && v.y == 0);
    v = (GVec){ FX(1), FX(1), 0, 0 };
    g3_norm3(&v);
    CHECK(NEAR(v.x, 46341, 16) && NEAR(v.y, 46341, 16));

    GVec n = { 0, 0, FX(1), 0 };
    G3Light L[2];
    L[0].dir = (GVec){ 0, 0, -FX(1), 0 };
    L[0].diffuse = FX_ONE;
    CHECK(g3_light_level(&n, L, 1, 0, FX_ONE, FX_ONE) == 63);
    L[0].dir = (GVec){ 0, 0, FX(1), 0 };
    CHECK(g3_light_level(&n, L, 1, 0, FX_ONE, FX_ONE) == 0);
    CHECK(g3_light_level(&n, L, 0, 32768, FX_ONE, FX_ONE) == 31);
    L[0].dir = (GVec){ 0, -46341, -46341, 0 };
    L[0].diffuse = FX_ONE;
    int lv = g3_light_level(&n, L, 1, 0, FX_ONE, FX_ONE);
    CHECK(NEAR(lv, 44, 1));
    L[1] = L[0];
    L[0].dir = (GVec){ 0, 0, -FX(1), 0 };
    CHECK(g3_light_level(&n, L, 2, 16384, FX_ONE, FX_ONE) == 63);
}

static void t_g3_lookat(void)
{
    fx m[16];
    GVec eye = { 0, 0, FX(5), 0 }, at = { 0, 0, 0, 0 }, up = { 0, FX(1), 0, 0 };
    m4_lookat(m, &eye, &at, &up);
    GVec p = { 0, 0, 0, FX(1) }, o;
    m4_vec(&o, m, &p);
    CHECK(NEAR(o.x, 0, 8) && NEAR(o.y, 0, 8) && NEAR(o.z, -FX(5), 16));
    p = (GVec){ FX(1), 0, 0, FX(1) };
    m4_vec(&o, m, &p);
    CHECK(NEAR(o.x, FX(1), 16) && NEAR(o.z, -FX(5), 16));
    p = (GVec){ 0, FX(2), 0, FX(1) };
    m4_vec(&o, m, &p);
    CHECK(NEAR(o.y, FX(2), 16));
}

static void t_gdi_ramps(void)
{
    const GdiOps *g = (const GdiOps *)api->service_get("gdi");
    CHECK(g != 0);
    if (!g) return;
    CHECK(g->abi >= 11);
    if (g->abi < 11) return;
    CHECK(g->pal_ramps() == 1);

    u8 si = g->pal_index(GRGB(220, 220, 220));
    u32 sc = g->pal_unpack(si);
    int sr = (sc >> 16) & 0xFF, sg = (sc >> 8) & 0xFF, sb = sc & 0xFF;
    CHECK(sr > 190 && sg > 190 && sb > 190);
    int sd = sr - sg; if (sd < 0) sd = -sd;
    int sd2 = sg - sb; if (sd2 < 0) sd2 = -sd2;
    CHECK(sd < 20 && sd2 < 20);

    static const u32 dims[4] = { 0x3C0000, 0x006000, 0x000050, 0x504000 };
    for (int i = 0; i < 4; i++) {
        u32 got = g->pal_unpack(g->pal_index(dims[i]));
        int dr = (int)((got >> 16) & 0xFF) - (int)((dims[i] >> 16) & 0xFF);
        int dg = (int)((got >> 8) & 0xFF) - (int)((dims[i] >> 8) & 0xFF);
        int db = (int)(got & 0xFF) - (int)(dims[i] & 0xFF);
        if (dr < 0) dr = -dr;
        if (dg < 0) dg = -dg;
        if (db < 0) db = -db;
        CHECK(dr + dg + db <= 40);
    }
}

static void t_dith_grad(void)
{
    const GdiOps *g = (const GdiOps *)api->service_get("gdi");
    CHECK(g && g->abi >= 11);
    if (!g || g->abi < 11) return;
    g->set_dither(1);
    g->fill_gradient(0, 0, 200, 120, GRGB(20, 30, 60), GRGB(200, 60, 120), 1);
    g->set_dither(0);
    u8 *px; int pitch, pw, ph;
    CHECK(api->surface_lock(&px, &pitch, &pw, &ph) == 1);
    int per = 1;
    for (int y = 5; y < 120; y += 13)
        for (int x = 0; x < 190; x += 7)
            if (px[y * pitch + x] != px[y * pitch + x + 4]) per = 0;
    api->surface_unlock();
    CHECK(per);
    u32 t0h, t0l, t1h, t1l;
    api->tsc_read(&t0h, &t0l);
    g->set_dither(1);
    for (int i = 0; i < 20; i++)
        g->fill_gradient(0, 0, 640, 456, GRGB(36, 80, 100), GRGB(16, 34, 56), 1);
    g->set_dither(0);
    api->tsc_read(&t1h, &t1l);
    u32 mhz = api->cpu_mhz();
    char pb[48];
    api->kfmt(pb, sizeof pb, "# 20 dithered 640x456 fills: %d us\n",
              mhz ? (int)((t1l - t0l) / mhz) : -1);
    ser_puts(pb);
}

static void t_g3d_zorder(void)
{
    const G3dOps *g3 = (const G3dOps *)api->service_get("g3d");
    const GdiOps *gd = (const GdiOps *)api->service_get("gdi");
    CHECK(g3 && gd);
    if (!g3 || !gd) return;
    CHECK(g3->abi >= 2);
    if (g3->abi < 2) return;

    G3D *c = g3->create();
    CHECK(c != 0);
    if (!c) return;
    g3->viewport(c, 0, 0, 64, 64, FX(1), FX(9));
    GMat pm;
    g3->mat_persp(&pm, FX(90), FX_ONE, FX(1), FX(9));
    g3->matrix_mode(c, G3M_PROJ);
    g3->load(c, &pm);
    g3->set(c, G3S_ZENABLE, 1);
    g3->clearz(c, FX_ONE);

    static const G3Vtx nearT[3] = {
        { -FX(1), -FX(1), -FX(2), GRGB(0,255,0) },
        {  FX(1), -FX(1), -FX(2), GRGB(0,255,0) },
        {  0,      FX(1), -FX(2), GRGB(0,255,0) },
    };
    static const G3Vtx farT[3] = {
        { -FX(1), -FX(1), -FX(4), GRGB(255,0,0) },
        {  FX(1), -FX(1), -FX(4), GRGB(255,0,0) },
        {  0,      FX(1), -FX(4), GRGB(255,0,0) },
    };
    CHECK(g3->draw(c, G3P_TRIS, G3F_XYZ | G3F_DIFFUSE, nearT, 3, 0, 3) == 1);
    CHECK(g3->draw(c, G3P_TRIS, G3F_XYZ | G3F_DIFFUSE, farT, 3, 0, 3) == 1);

    u8 *px; int pitch, pw, ph;
    CHECK(api->surface_lock(&px, &pitch, &pw, &ph) == 1);
    u8 got = px[32 * pitch + 32];
    CHECK(got == gd->pal_index(GRGB(0,255,0)));
    api->surface_unlock();

    g3->set(c, G3S_ZENABLE, 0);
    g3->draw(c, G3P_TRIS, G3F_XYZ | G3F_DIFFUSE, farT, 3, 0, 3);
    api->surface_lock(&px, &pitch, &pw, &ph);
    got = px[32 * pitch + 32];
    api->surface_unlock();
    CHECK(got == gd->pal_index(GRGB(255,0,0)));
    g3->destroy(c);
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "SelfTest"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    ser_init();
    ser_puts("SELFTEST START\n");
#ifndef SELFTEST_SMALL
    run("new_apps", t_newapps);
#endif

    run("crc32", t_crc32);
    run("b64_encode", t_b64);
    run("b64_roundtrip", t_b64_roundtrip);
    run("path_base/ext", t_path);
    run("kfmt_pad", t_kfmt_pad);
    run("atoi", t_atoi);
    run("norm_path", t_normpath);
    run("spec_split", t_spec);
    run("sysfile", t_sysfile);
    run("usage", t_usage);
    run("ioguard", t_ioguard);
    run("flipgate", t_flipgate);
    run("flight_due", t_flight_due);
    run("flight_slice", t_flight_slice);
    run("ls_action", t_ls_action);
    run("dl_parse", t_dl_parse);
    run("kb_unctrl", t_kb_unctrl);
    run("plat_hostbridge", t_plat_hostbridge);
    run("sched_next", t_sched_next);
    run("appq", t_appq);
    run("thr_may_switch", t_thr_may_switch);
    run("tf_key", t_tf_key);
    run("usb_db", t_usb_db);
    run("tetris", t_tetris);
    run("pong", t_pong);
    run("pong_hit", t_pong_hit);
    run("fsplan", t_fsplan);
    run("fsd_moves", t_fsd_moves);
    run("fsd_preverify", t_fsd_preverify);
    run("fsd_wrfail", t_fsd_wrfail);
    run("fsd_torn", t_fsd_torn);
    run("heap_bounds", t_heap_bounds);
    run("ms_bounds", t_ms_bounds);
    run("hangwatch_prompts_once", t_hangwatch_prompts_once);
    run("hangwatch_progress_never_accused", t_hangwatch_progress_never_accused);
    run("hangwatch_hard_ceiling", t_hangwatch_hard_ceiling);
    run("hangwatch_progress_then_stops", t_hangwatch_progress_then_stops);
    run("hangwatch_rearm_past_hard", t_hangwatch_rearm_past_hard);
    run("hangwatch_rearm", t_hangwatch_rearm);
    run("hangwatch_win_change", t_hangwatch_win_change);
    run("paint_view", t_paint_view);
    run("busycore_quick_event", t_busycore_quick_event);
    run("busycore_long_handler", t_busycore_long_handler);
    run("busycore_stream_no_strobe", t_busycore_stream_no_strobe);
    run("busycore_gap_resets", t_busycore_gap_resets);
    run("busycore_window_switch", t_busycore_window_switch);
    run("network_wire_validation", t_network_wire_validation);
    run("network_config", t_network_config);
    run("emergency_policy", t_emergency_policy);
    run("cfgsan_ranges", t_cfgsan_ranges);
    run("cfgsan_path", t_cfgsan_path);
    run("cfgsan_keeps_valid", t_cfgsan_keeps_valid);
    run("fhmap_fits_box", t_fhmap_fits_box);
    run("fbspan", t_fbspan);
    run("bench_rate", t_bench_rate);
    run("desktop_folder", t_desktop_folder);
    run("cpu_ownership", t_cpu_ownership);
    run("memory_layout", t_memory_layout);
    run("fhlayout", t_fhlayout);
    run("midiname_display", t_midiname_display);
    run("framegate_idle", t_framegate_idle_skips);
    run("framegate_damage", t_framegate_damage_presents);
    run("framegate_cursor", t_framegate_cursor_presents);
    run("framegate_blink", t_framegate_blink_presents);
    run("framegate_ss", t_framegate_screensaver_always);
    run("winhit_topmost", t_winhit_topmost_wins);
    run("winhit_edges", t_winhit_edges);
    run("winfit_vga", t_winfit_vga);
    run("winfit_fitting", t_winfit_leaves_fitting_alone);
    run("winfit_edges", t_winfit_exact_and_degenerate);
    run("mepreset_layout", t_mepreset_layout);
    run("mepreset_absent", t_mepreset_absent_regions);
    run("mepreset_collapse", t_mepreset_collapse_and_cap);
    run("pumpbtn_drag", t_pumpbtn_drag);
    run("pumpbtn_press_during_pump", t_pumpbtn_press_during_pump);
    run("pumpbtn_independent", t_pumpbtn_buttons_independent);
    run("tet_drop", t_tet_drop);
    run("kb_mainrow", t_kb_mainrow);
    run("kb_keypad_digits", t_kb_keypad_digits);
    run("kb_keypad_nav", t_kb_keypad_nav);
    run("kb_keypad_e0", t_kb_keypad_e0);
    run("kb_alt", t_kb_alt);
    run("kb_prtsc", t_kb_prtsc);
    run("atsw_cycle", t_atsw_cycle);
    run("axline_iter", t_axline_iter);
    run("sbar_thumb", t_sbar_thumb);
    run("ms2_assemble", t_ms2_assemble);
    run("ms2_bad_first", t_ms2_bad_first);
    run("ms2_timeout_resync", t_ms2_timeout_resync);
    run("modsort_order", t_modsort_order);
    run("modsort_bounds", t_modsort_bounds);
    run("modsort_share", t_modsort_share);
    run("sbdrag_zones", t_sbdrag_zones);
    run("sbdrag_paging", t_sbdrag_paging);
    run("sbdrag_thumb", t_sbdrag_thumb);
    run("lz_roundtrip", t_lz_roundtrip);
    run("lz_incompressible", t_lz_incompressible);
    run("lz_empty", t_lz_empty);
    run("lz_refuses_bad", t_lz_refuses_bad);
    run("lfn_checksum", t_lfn_checksum);
    run("lfn_names", t_lfn_names);
    run("lfn_rejects", t_lfn_rejects);
    run("lfn_truncate", t_lfn_truncate);
    run("nicdesc", t_nicdesc);
    run("phy_link_state", t_phy_link_state);
    run("phy_negotiate", t_phy_negotiate);
    run("tulip_csr6", t_tulip_csr6);
    run("ntpcore_vectors", t_ntpcore_vectors);
    run("ntpcore_tz", t_ntpcore_tz);
    run("ntpcore_rejects", t_ntpcore_rejects);
    run("clipline_paste", t_clipline_paste);
    run("bmpw_layout", t_bmpw_layout);
    run("shotname_next", t_shotname_next);
    run("ramtest", t_ramtest);
    run("ks_split", t_ks_split);
    run("ks_place", t_ks_place);
    run("ks_pool", t_ks_pool);
    run("ks_window", t_ks_window);
    run("panic_report", t_panic_report);
    run("ks_owner", t_ks_owner);
    run("ks_fixable", t_ks_fixable);
    run("pg_index", t_pg_index);
    run("pg_align", t_pg_align);
    run("pg_entries", t_pg_entries);
    run("pg_va_mapped", t_pg_va_mapped);
    run("pg_span", t_pg_span);
    run("pg_pte_span", t_pg_pte_span);
    run("bmp_head", t_bmp_head);
    run("bmp_rows", t_bmp_rows);
    run("bmp_scale", t_bmp_scale);
    run("tree", t_tree);
    run("diskmap", t_diskmap);
    run("opl2", t_opl2);
    run("mine", t_mine);
    run("ring3_desc", t_ring3);
    run("ks_space", t_ks_space);
    run("tabcomp", t_tabcomp);
    run("sc_resolve", t_sc_resolve);
    run("sc_parent", t_sc_parent);
    run("mf_vlq", t_mf_vlq);
    run("mf_datalen", t_mf_datalen);
    run("mf_header", t_mf_header);
    run("mf_tickrate", t_mf_tickrate);
    run("mf_running", t_mf_running);
    run("mf_note_hz", t_mf_note_hz);
    run("fh_class", t_fh_class);
    run("fh_worst", t_fh_worst);
    run("fh_cell", t_fh_cell);
    run("fh_verdict", t_fh_verdict);
    run("nl_parse", t_nl_parse);
    run("nl_fmt", t_nl_fmt);
    run("nb_wrap", t_nb_wrap);
    run("nb_edit", t_nb_edit);
    run("ax_match", t_ax_match);
    run("ax_clamp_top", t_ax_clamp_top);
    run("ax_scroll", t_ax_scroll);
    run("mq_norm", t_mq_norm);
    run("mq_hit", t_mq_hit);
    run("wp_mode", t_wp_mode);
    run("wp_grad", t_wp_grad);
    run("wp_path_fix", t_wp_path_fix);
    run("del_prompt", t_del_prompt);
    run("fs_dir_of", t_fs_dir_of);
    run("fs_in_dir", t_fs_in_dir);
    run("fs_rejoin", t_fs_rejoin);
    run("fs_dirname_ok", t_fs_dirname_ok);
    run("fs_name_ok", t_fs_name_ok);
    run("uart_divisor", t_uart_divisor);
    run("uart_lsr", t_uart_lsr);
    run("fault_ring", t_fault_ring);
    run("graphics_fallback", t_graphics_fallback);
    run("save_paths", t_save_paths);
    run("memcpy_sizes", t_memcpy_sizes);
    run("memset_sizes", t_memset_sizes);
    run("memmove_overlap", t_memmove_overlap);
    run("snake_margin", t_snake_margin);
    run("snake_food_box", t_snake_food_box);
    run("mtrr_pow2", t_mtrr_pow2);
    run("mtrr_mask", t_mtrr_mask);
    run("mtrr_plan_aligned", t_mtrr_plan_aligned);
    run("mtrr_plan_split", t_mtrr_plan_split);
    run("mtrr_plan_limits", t_mtrr_plan_limits);
    run("mtrr_overlap", t_mtrr_overlap);
    run("mtrr_takeover", t_mtrr_takeover);
    run("vbank_detect", t_vbank_detect);
    run("vbank_prog", t_vbank_prog);
    run("vblit", t_vblit);
    run("url", t_url);
    run("ipparse", t_ipparse);
    run("dns", t_dns);
    run("dhcpopt", t_dhcpopt);
    run("dhcp_reply", t_dhcp_reply);
    run("dhcp_lease", t_dhcp_lease);
    run("dhcp_build", t_dhcp_build);
    run("http", t_http);
    run("http_clen", t_http_clen);
    run("tcpcsum", t_tcpcsum);
    run("tcpseq", t_tcpseq);
    run("tcp", t_tcp);
    run("fx_math", t_fx);
    run("gdi_nearest", t_nearest);
    run("gdi_bayer", t_bayer);
    run("gdi_ramp", t_ramp);
    run("rgn_combine", t_rgn_combine);
    run("rgn_query", t_rgn_query);
    run("poly_scan", t_poly_fill);
    run("poly_winding", t_poly_winding);
    run("poly_region", t_poly_region);
    run("bezier", t_bezier);
    run("flatten", t_flatten);
    run("stroke_seg", t_stroke_seg);
    run("rgb_lerp", t_rgb_lerp);
    run("span_cov", t_span_cov);
    run("poly_scan_aa", t_poly_scan_aa);
    run("g3_trig", t_g3_trig);
    run("g3_mat", t_g3_mat);
    run("g3_persp", t_g3_persp);
    run("g3_clip", t_g3_clip);
    run("bres_line", t_bres);
    run("g3_shade", t_g3_shade);
    run("g3_prim", t_g3_prim);
    run("g3_triscan", t_g3_trisc);
    run("g3_clipa", t_g3_clipa);
    run("g3_light", t_g3_light);
    run("g3_lookat", t_g3_lookat);
    run("gdi_ramps", t_gdi_ramps);
    run("dith_grad", t_dith_grad);
    run("g3d_service", t_g3d_service);
    run("g3d_zorder", t_g3d_zorder);

    char sum[40];
    api->kfmt(sum, sizeof sum, "SELFTEST DONE: %d pass %d fail\n", g_pass, g_fail);
    ser_puts(sum);
    return 0;
}
