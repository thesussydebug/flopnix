/* Built-in shell commands and terminal output. */
#include "os.h"
#include "shpath.h"
#include "../kexts/net_wire.inc"
#include "ramtest.inc"
#include "appq.inc"
#include "busycore.inc"
#include "clipline.inc"
#include "shcwd.inc"
#include "tabcomp.inc"
#include "shcmd.inc"
#include "shspec.inc"

extern char __bss_start[], __bss_end[], __load_end[];

int net_parse_ip(const char *s, u32 *out);
static void sh_dispatch(char *cmd);

#define TCMAX 80
#define SBMAX 160
#define THIST 12
#define PROMPT "root@flopnix:~# "

typedef struct {
    char sb[SBMAX][TCMAX];
    int  head;
    int  nlines;
    int  cx;
    int  view;
    int  cols, rows;
    u8   rainbow, fx;

    char line[192];
    char pending[192];
    u8 running;
    int  len;
    int  inpx;
    char hist[THIST][192];
    int  hist_n, hist_pos;

    char cwd[SC_MAX];

    char tab_pre[32];
    int  tab_idx;
    u8   tab_on;
} Term;

static Term terms[MAXINST];
static Term *term_context[THR_MAX],*term_last;
#define T term_context[thr_self]
static int tdcols, tdrows;

static char *line_at(Term *t, int i) { return t->sb[(t->head + i) % SBMAX]; }
static char *cur_line(void) { return line_at(T, T->nlines - 1); }

static void tnl(void)
{
    T->cx = 0;
    if (T->nlines < SBMAX) T->nlines++;
    else T->head = (T->head + 1) % SBMAX;
    memset(cur_line(), 0, TCMAX);
}
static void tputc(char c)
{
    T->view = 0;

    gui_dirty = 1;
    if (c == '\n') { tnl(); return; }
    if (c == '\b') { if (T->cx > 0) { T->cx--; cur_line()[T->cx] = 0; } return; }
    if (T->cx >= TCMAX - 1) tnl();
    cur_line()[T->cx] = c;
    if (++T->cx >= T->cols) tnl();
}
static void tprint(const char *s) { while (*s) tputc(*s++); }

static void tdraw_input(void)
{
    char *row = cur_line();
    int avail = T->cols - T->inpx - 1;
    if (avail < 1) avail = 1;
    int start = 0, col = T->inpx;
    if (T->len > avail) {

        start = T->len - avail + 1;
        row[col++] = '<';
    }
    while (col < T->cols - 1 && T->line[start]) row[col++] = T->line[start++];
    for (int j = col; j < TCMAX; j++) row[j] = 0;
    T->cx = col < TCMAX ? col : TCMAX - 1;
    T->view = 0;
    gui_dirty = 1;
}

static void tprompt(void)
{
    char p[80];
    kfmt(p, sizeof p, "root@flopnix:/%s# ", T->cwd);
    tprint(p);
    T->len = 0;
    T->line[0] = 0;
    T->inpx = T->cx;
    T->running=0;
    if(T->pending[0]){strlcpy(T->line,T->pending,sizeof T->line);T->len=strlen(T->line);T->pending[0]=0;tdraw_input();}
}

const char *shell_cwd_get(void) { return T ? T->cwd : ""; }

int shell_cwd_set(const char *d)
{
    if (!T || !d) return 0;
    if ((int)strlen(d) >= SC_MAX) return 0;
    strlcpy(T->cwd, d, sizeof T->cwd);
    return 1;
}

static void term_set_input(const char *s)
{
    T->len = 0;
    for (const char *p = s; *p && T->len < (int)sizeof(T->line) - 1; p++)
        T->line[T->len++] = *p;
    T->line[T->len] = 0;
    tdraw_input();
}
static void hist_push(const char *s)
{
    if (!s[0]) return;
    if (T->hist_n > 0 && !strcmp(T->hist[(T->hist_n - 1) % THIST], s)) { T->hist_pos = T->hist_n; return; }
    strlcpy(T->hist[T->hist_n % THIST], s, sizeof T->hist[0]);
    T->hist_n++;
    T->hist_pos = T->hist_n;
}
static void hist_recall(int dir)
{
    int lo = T->hist_n > THIST ? T->hist_n - THIST : 0;
    int np = T->hist_pos + dir;
    if (np < lo) np = lo;
    if (np > T->hist_n) np = T->hist_n;
    T->hist_pos = np;
    term_set_input(np == T->hist_n ? "" : T->hist[np % THIST]);
}

u32 used_kb(void)
{

    u32 img = (u32)__load_end - 0x8000;
    u32 bss = (u32)__bss_end - (u32)__bss_start;
    return (img + bss + IOBUF_SZ + (u32)(SW * SH) + 1023 +
            kapi.mem_info(MI_ARENA_RO) + kapi.mem_info(MI_ARENA_RW) +
            kapi.mem_info(MI_POOL_USED) + heap_end() - heap_base() - heap_avail()) / 1024 + 120;
}

void shell_print(const char *s) { if(!T)T=term_last;if(T)tprint(s); }

void term_clear(void)
{
    if (!T) return;
    memset(T->sb, 0, sizeof T->sb);
    T->head = T->cx = T->view = 0;
    T->nlines = 1;
}
int term_fx(int mode)
{
    if (!T) return 0;
    if (mode == 1) return T->fx = 1;
    if (mode == 2) return T->rainbow = !T->rainbow;
    return 0;
}
const char *term_hist(int i)
{
    if (!T || i < 0) return 0;
    int count = T->hist_n > THIST ? THIST : T->hist_n;
    if (i >= count) return 0;
    int lo = T->hist_n > THIST ? T->hist_n - THIST : 0;
    return T->hist[(lo + i) % THIST];
}
int shell_win_close(int i)
{
    if (i < 0 || i >= MAXWIN || !wins[i].used) return 0;
    if (wins[i].type == WT_TERM && &terms[wins[i].inst] == T) return -1;
    win_close(i);
    return 1;
}

__attribute__((minsize)) void dmesg_print(void)
{
    char buf[96];
    u8 d = BOOTINFO->diag;
    kfmt(buf, sizeof buf, "boot stage %d/8, diag %02x\n", BOOTINFO->stage, d);
    tprint(buf);
    tprint("loader:  kernel loaded (single-sector reads)\n");
    {
        extern u8 cfg_was_blank;
        if (d & BD_CFG_HUNG)       tprint("config:  READ HUNG - watchdog skipped it\n");
        else if (!(d & BD_CFG_OK)) tprint("config:  read failed - using defaults\n");
        else if (cfg_was_blank)    tprint("config:  blank - using defaults\n");
        else                       tprint("config:  loaded from disk\n");
    }
    kfmt(buf, sizeof buf, "font:    %s\n",
         (d & BD_FONT_HUNG) ? "GRAB HUNG (E01) - text may be blank" : "bios 8x16 ok");
    tprint(buf);
    kfmt(buf, sizeof buf, "memory:  %u MB%s\n", BOOTINFO->mem_kb / 1024,
         (d & BD_MEM_HUNG) ? " (DETECT HUNG, E02 - assumed 4 MB)" : "");
    tprint(buf);
    kfmt(buf, sizeof buf, "video:   %dx%dx8 %s%s%s\n", SW, SH,
         BOOTINFO->vbe == 2 ? "VESA banked" :
         BOOTINFO->vbe     ? "VESA LFB" : "VGA",
         (d & BD_VBE_HUNG) ? " (VESA HUNG, E03 - fell back)" : "",
         (d & BD_VGA_HUNG) ? " (VGA SET HUNG, E04)" : "");
    tprint(buf);
    kfmt(buf, sizeof buf, "a20:     %s\n",
         (d & BD_A20_HUNG) ? "BIOS CALL HUNG (E05) - used controller" : "ok");
    tprint(buf);
    tprint((d & BD_ANY_HANG) ? "watchdog recovered from hung BIOS call(s)\n"
                             : "no BIOS hangs caught\n");
    kfmt(buf, sizeof buf, "timer:   %s\n",
         timer_alive ? "tick alive" : "TICK DEAD (E10) - delays estimated");
    tprint(buf);
    tprint("irqs:   ");
    for (int i = 0; i < 16; i++) {
        if (!irq_counts[i]) continue;
        kfmt(buf, sizeof buf, " %d:%u", i, irq_counts[i]);
        tprint(buf);
    }
    tputc('\n');
    if (boot_errs[0]) {
        kfmt(buf, sizeof buf, "self-check FAILED:%s (see README)\n", boot_errs);
        tprint(buf);
    } else tprint("self-check: all ok\n");
    {
        static char kl[2048];
        if (klog_read(kl, sizeof kl) > 0) {
            tprint("--- kernel log ---\n");
            tprint(kl);
            if (kl[strlen(kl) - 1] != '\n') tputc('\n');
        }
    }
    {
        int ov = thread_overflowed();
        if (ov >= 0) {
            char tb[56];
            kfmt(tb, sizeof tb, "thread %d blew its stack (killed)\n", ov);
            tprint(tb);
        }
    }
    {

        char lp[160];
        loop_prof_fmt(lp, sizeof lp);
        tprint("--- loop (ms) ---\n");
        tprint(lp);
        tputc('\n');
        loop_prof_reset();
    }
}

void threads_report(char *out, int cap)
{
    static const char *st[] = { "unused", "ready", "running", "waiting",
                                "finished" };
    int o = 0;
    #define RP(...) do { kfmt(out + o, cap - o, __VA_ARGS__); \
                         o += (int)strlen(out + o); } while (0)

    RP("FLOPNIX %s threads - preemption is %s\n\n", OS_VER,
       thr_preempt_on() ? "ON" : "off");
    RP("  #  name        state     switches  deepest stack use\n");
    RP("  -  ----------  --------  --------  -----------------\n");

    int live = 0, done = 0;
    u32 total = 0;
    for (int i = 0; i < THR_MAX && o < cap - 120; i++) {
        ThreadInfo t;
        if (!thread_info(i, &t)) continue;
        if (t.state == THR_DEAD) done++; else live++;
        total += t.runs;

        char nm[11], sw[9];
        int j = 0;
        for (; t.name[j] && j < 10; j++) nm[j] = t.name[j];
        while (j < 10) nm[j++] = ' ';
        nm[10] = 0;
        const char *s = t.state <= 4 ? st[t.state] : "?";
        j = 0;
        for (; s[j] && j < 8; j++) sw[j] = s[j];
        while (j < 8) sw[j++] = ' ';
        sw[8] = 0;

        if (i == 0) {
            RP("  %d  %s  %s  %8u  (boot stack, not measured)\n",
               i, nm, sw, t.runs);
        } else {
            u32 pct = t.stack_size ? t.stack_used * 100 / t.stack_size : 0;
            RP("  %d  %s  %s  %8u  %u of %u bytes (%u%%)\n",
               i, nm, sw, t.runs, t.stack_used, t.stack_size, pct);
        }
    }

    RP("\n  %d running, %d finished, %u context switches since boot.\n",
       live, done, total);
    RP("  Each thread gets %u KB of stack; the figure above is the deepest\n"
       "  it has ever gone, not what it is using now.\n", 16384u / 1024);

    for (int i = 1; i < THR_MAX && o < cap - 200; i++) {
        ThreadInfo t;
        if (!thread_info(i, &t) || !t.stack_size) continue;
        u32 pct = t.stack_used * 100 / t.stack_size;
        if (pct >= 75)
            RP("\n  WARNING: thread %d (%s) has used %u%% of its stack.\n"
               "  It is close to overflowing. Raise THR_STACK in src/thread.c.\n",
               i, t.name, pct);
    }
    int ov = thread_overflowed();
    if (ov >= 0)
        RP("\n  ERROR: thread %d ran off the end of its stack and was killed.\n"
           "  Whatever it was doing was abandoned. Memory below that stack may\n"
           "  have been corrupted before it was caught. Raise THR_STACK.\n", ov);
    #undef RP
}

void threads_print(void)
{
    static char rep[1400];
    threads_report(rep, sizeof rep);
    tprint(rep);

    char b[80];
    if (!usb_present())          tprint("\n(no USB stick - report not saved)\n");
    else if (!fat_mount())       tprint("\n(USB not readable - report not saved)\n");
    else if (!fat_writable())    tprint("\n(USB is read-only - report not saved)\n");
    else if (fat_write("THREADS.TXT", (const u8 *)rep, strlen(rep)) != 0)
                                 tprint("\n(USB write failed - report not saved)\n");
    else {
        kfmt(b, sizeof b, "\nSaved to USB as THREADS.TXT (%u bytes)\n",
             (u32)strlen(rep));
        tprint(b);
    }
}

int apps_animating(void)
{
    for (int i = 0; i < MAXINST; i++)
        if (terms[i].fx || terms[i].rainbow) return 1;
    return 0;
}

static void term_reset(int inst)
{
    T = &terms[inst];term_last=T;
    memset(T, 0, sizeof *T);
    T->nlines = 1;
    T->cols = tdcols;
    T->rows = tdrows;
    T->hist_pos = 0;
    tprint(OS_NAME " " OS_VER " - a floppy-sized OS\n");
    tprint("type 'help' for commands\n");
    if (BOOTINFO->diag & BD_ANY_HANG)
        tprint("boot: recovered from a BIOS hang - see 'dmesg'\n");
    tprint("\n");
    tprompt();
}

static void term_scroll(Term *t, int lines)
{
    int maxv = t->nlines - t->rows;
    if (maxv < 0) maxv = 0;
    t->view += lines;
    if (t->view < 0) t->view = 0;
    if (t->view > maxv) t->view = maxv;
}

void term_wheel(int inst, int dz) { term_scroll(&terms[inst], dz * 3); }
static void term_drop(int inst,int x,int y,const char *type,const char *data)
{
    (void)x;(void)y;
    if(!type||!data||(strcmp(type,"file")&&strcmp(type,"file.cut")))return;
    win_focus(WT_TERM,inst);
    preempt_disable();
    Term *t=&terms[inst],*saved=T;
    if(t->running)sp_insert(t->pending,strlen(t->pending),sizeof t->pending,data);
    else{T=t;t->len=sp_insert(t->line,t->len,sizeof t->line,data);t->tab_on=0;tdraw_input();T=saved;}
    preempt_enable();
}

static void tab_replace(int wordstart, const char *s)
{
    while (T->len > wordstart) { T->len--; tputc('\b'); }
    for (int i = 0; s[i] && T->len < (int)sizeof T->line - 1; i++) {
        T->line[T->len++] = s[i];
        tputc(s[i]);
    }
}

static void term_tab(void)
{
    for (int i = 0; i < T->len; i++)
        if (T->line[i] == ' ') return;

    if (!T->tab_on) {
        int n = T->len < (int)sizeof T->tab_pre - 1 ? T->len
                                                    : (int)sizeof T->tab_pre - 1;
        for (int i = 0; i < n; i++) T->tab_pre[i] = T->line[i];
        T->tab_pre[n] = 0;
        T->tab_idx = 0;
        T->tab_on = 1;
    }

    int m = tc_count(T->tab_pre, sh_names, SH_NNAMES);
    if (!m) { T->tab_on = 0; return; }

    const char *c = tc_nth(T->tab_pre, sh_names, SH_NNAMES, T->tab_idx % m);
    T->tab_idx++;
    if (!c) return;
    tab_replace(0, c);
    if (m == 1) {
        if (T->len < (int)sizeof T->line - 1) { T->line[T->len++] = ' '; tputc(' '); }
        T->tab_on = 0;
    }
}

static void term_key(int inst, int k)
{
    T = &terms[inst];term_last=T;
    if (T->fx) { T->fx = 0; tprompt(); return; }
    if (k == K_UP)   { hist_recall(-1); return; }
    if (k == K_DOWN) { hist_recall(1); return; }
    if (k == K_PGUP) { term_scroll(T, T->rows - 1); return; }
    if (k == K_PGDN) { term_scroll(T, -(T->rows - 1)); return; }
    if (k >= 0x100) return;
    char ch = (char)k;
    if (ch == '\t') { term_tab(); return; }
    T->tab_on = 0;
    if (ch == '\n') {
        tputc('\n');
        T->line[T->len] = 0;
        hist_push(T->line);
        char command[sizeof T->line];strlcpy(command,T->line,sizeof command);T->running=1;
        sh_dispatch(command);
        T->running=0;
        if (!T->fx) tprompt();
        return;
    }
    if (ch == '\b') {
        if (T->len > 0) { T->line[--T->len] = 0; tdraw_input(); }
        return;
    }
    if (ch == 0x16) {
        char clip[192];
        if (clip_get_text(clip, sizeof clip) > 0) {
            T->len = cl_paste(T->line, T->len, sizeof T->line, clip);
            tdraw_input();
        }
        return;
    }
    if (ch == 0x03) {
        char out[4096];
        int o = 0;
        int first = T->nlines - T->rows - T->view;
        if (first < 0) first = 0;
        int last = T->nlines - T->view;
        for (int i = first; i < last; i++) {
            const char *l = line_at(T, i);
            for (int x = 0; l[x] && x < TCMAX && o < (int)sizeof out - 2; x++)
                out[o++] = l[x];
            if (o < (int)sizeof out - 1) out[o++] = '\n';
        }
        out[o] = 0;
        clip_set_text(out);
        return;
    }
    if (ch < 32) return;

    if (T->len < (int)sizeof(T->line) - 1) {
        T->line[T->len++] = ch;
        T->line[T->len] = 0;
        tdraw_input();
    }
}

static const u8 rain_hue[6] = { C_RED, C_YELLOW, C_BGREEN, C_CYAN, C_BBLUE, C_MAGENTA };

static void matrix_draw(int cx, int cy, int cols, int rows)
{
    u32 fr = ticks / 2;
    int period = rows + 14;
    for (int c = 0; c < cols; c++) {
        int head = (int)((fr + c * 7) % period);
        for (int r = 0; r < rows; r++) {
            int d = head - r;
            if (d < 0 || d >= 11) continue;
            char g = 33 + (char)((c * 31 + r * 17 + fr / 8) % 94);
            u8 col = d == 0 ? C_WHITE : (d < 3 ? C_BGREEN : C_GREEN);
            draw_char(cx + 4 + c * 8, cy + 4 + r * 16, g, col);
        }
    }
}

static void term_draw(Win *w, int cx, int cy, int cw, int ch)
{
    Term *t = &terms[w->inst];
    int cols = (cw - 8) / 8, rows = (ch - 8) / 16;
    if (cols > TCMAX) cols = TCMAX; if (cols < 8) cols = 8;
    if (rows > 44) rows = 44; if (rows < 3) rows = 3;
    t->cols = cols; t->rows = rows;

    fill_rect(cx, cy, cw, ch, C_TERMBG);

    if (t->fx) { matrix_draw(cx, cy, cols, rows); return; }

    int maxv = t->nlines - rows; if (maxv < 0) maxv = 0;
    if (t->view > maxv) t->view = maxv;
    int firstvis = t->nlines - rows - t->view;
    if (firstvis < 0) firstvis = 0;
    for (int r = 0; r < rows; r++) {
        int li = firstvis + r;
        if (li < 0 || li >= t->nlines) continue;
        char *ln = line_at(t, li);
        for (int c = 0; c < cols; c++) {
            if (!ln[c] || ln[c] == ' ') continue;
            u8 col = t->rainbow ? rain_hue[(c + r + ticks / 6) % 6] : C_TERMFG;
            draw_char(cx + 4 + c * 8, cy + 4 + r * 16, ln[c], col);
        }
    }
    if (t->view > 0) {
        char tag[20];
        kfmt(tag, sizeof tag, "-- %d up --", t->view);
        draw_text(cx + cw - strlen(tag) * 8 - 8, cy + 2, tag, C_YELLOW);
    }
    int crow = (t->nlines - 1) - firstvis;
    if (win_is_focused(w) && gui_blink && t->view == 0 && crow >= 0 && crow < rows && t->cx < cols)
        fill_rect(cx + 4 + t->cx * 8, cy + 4 + crow * 16 + 14, 8, 2, C_TERMFG);
}

const char *cpu_brand(void);
u32 cpu_mhz(void);

static void sysinfo_draw(Win *w, int cx, int cy)
{
    int cw = w->w - 6;
    char b[96], hs[20];
    int x = cx + 10, y = cy + 8;
    draw_text(x, y, OS_NAME " " OS_VER, C_NAVY);
    hline(cx + 8, cy + 29, cw - 16, C_SHAD);
    y += 32;
    const char *labels[7] = {"Processor", "Memory", "Screen", "Floppy", "USB storage", "Network", "Running for"};
    for (int i = 0; i < 7; i++, y += 24) {
        if (!(i & 1)) fill_rect(cx + 8, y - 3, cw - 16, 22, C_G0 + 7);
        switch (i) {
        case 0: kfmt(b, sizeof b, "%s, %u MHz", cpu_brand(), cpu_mhz()); break;
        case 1: human_size_kb(BOOTINFO->mem_kb, hs, sizeof hs); kfmt(b, sizeof b, "%s usable RAM", hs); break;
        case 2: kfmt(b, sizeof b, "%d x %d, 256 colours", SW, SH); break;
        case 3: strlcpy(b, "A: - 1.44 MB", sizeof b); break;
        case 4: strlcpy(b, usb_present() ? usb_model() : "No drive connected", sizeof b); break;
        case 5: {
            u32 ip = net_get(NET_IP);
            if (!net_up()) strlcpy(b, "No supported network card", sizeof b);
            else if (!ip) strlcpy(b, "Waiting for an address", sizeof b);
            else kfmt(b, sizeof b, "%u.%u.%u.%u (%s)", ip & 255, ip >> 8 & 255, ip >> 16 & 255, ip >> 24,
                      net_get(NET_DHCP_OK) ? "automatic" : "manual");
            break;
        }
        default: { u32 secs = ticks / 100; kfmt(b, sizeof b, "%u h %u min %u sec", secs / 3600, secs / 60 % 60, secs % 60); }
        }
        draw_text(x, y, labels[i], C_G0 + 2);
        draw_text_clip(x + 110, y, b, C_BLACK, cw - 138);
    }
}

static AppDesc regs[MAX_APPS];

static int reg_owner[MAX_APPS];
static u8 reg_used[MAX_APPS][MAXINST];
static int nregs;

static u8 in_shell_exec;

static void sh_dispatch(char *cmd)
{
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;
    char c0[16];
    int ci = 0;
    while (cmd[ci] && cmd[ci] != ' ' && ci < 15) { c0[ci] = cmd[ci]; ci++; }
    c0[ci] = 0;
    const char *cargs = cmd + ci;
    while (*cargs == ' ') cargs++;
    int help = !strcmp(cargs, "/help") || !strcmp(cargs, "-h") ||
               !strcmp(cargs, "--help");
    if (!help && cmd_dispatch(c0, cargs)) return;
    if (shell_fallback(cmd)) return;
    tprint("shell.kx not loaded - only registered commands work\n");
}

int shell_exec(const char *line)
{
    if (!line || !line[0] || in_shell_exec) return -1;
    int inst = -1;
    for (int i = 0; i < MAXINST; i++)
        if (reg_used[WT_TERM][i]) inst = i;
    if (inst < 0) return -1;
    T = &terms[inst];term_last=T;
    if(T->running)return -1;
    char buf[192];
    strlcpy(buf, line, sizeof buf);
    in_shell_exec = 1;
    tputc('\n');
    sh_dispatch(buf);
    tprompt();
    in_shell_exec = 0;
    gui_dirty = 1;
    return 0;
}

static Term *newest_term(void)
{
    for (int i = MAXINST - 1; i >= 0; i--)
        if (reg_used[WT_TERM][i]) return &terms[i];
    return 0;
}
int shell_history_count(void)
{
    Term *t = newest_term();
    if (!t) return 0;
    return t->hist_n < THIST ? t->hist_n : THIST;
}
const char *shell_history(int i)
{
    Term *t = newest_term();
    if (!t) return "";
    int lo = t->hist_n > THIST ? t->hist_n - THIST : 0;
    int idx = lo + i;
    if (i < 0 || idx >= t->hist_n) return "";
    return t->hist[idx % THIST];
}

int register_app(const AppDesc *d)
{
    int replace = kext_lazy_type();
    if (replace >= 0 && replace < nregs && d && d->title && !strcmp(d->title,regs[replace].title) && d->draw && d->client_size) {
        regs[replace] = *d; reg_owner[replace] = kext_owner_now();
        if (regs[replace].max_inst < 1) regs[replace].max_inst = 1;
        if (regs[replace].max_inst > MAXINST) regs[replace].max_inst = MAXINST;
        return replace;
    }

    if (nregs >= MAX_APPS) {
        char m[72];
        kfmt(m, sizeof m, "register_app: registry full at %d - '%s' rejected",
             MAX_APPS, (d && d->title) ? d->title : "?");
        klog(m); klog("\n");
        return -1;
    }
    if (!d || !d->title || !d->draw || !d->client_size)
        return -1;
    regs[nregs] = *d;
    reg_owner[nregs] = kext_owner_now();
    if (regs[nregs].max_inst < 1) regs[nregs].max_inst = 1;
    if (regs[nregs].max_inst > MAXINST) regs[nregs].max_inst = MAXINST;
    return nregs++;
}

int app_type_owned(int owner) {
    if (owner < 0) return -1;
    for (int i = 0; i < nregs; i++) if (reg_owner[i] == owner) return i;
    return -1;
}
int app_count(void) { return nregs; }
void app_placeholder(int type,const AppDesc *desc)
{
    if(type<0||type>=nregs)return;
    regs[type]=*desc;reg_owner[type]=-1;memset(reg_used[type],0,MAXINST);
}
const AppDesc *app_desc(int t) { return (t >= 0 && t < nregs) ? &regs[t] : 0; }

int app_find(const char *title)
{
    for (int i = 0; i < nregs; i++)
        if (!strcmp(regs[i].title, title)) return i;
    return -1;
}

int app_multi(int t) { return (t >= 0 && t < nregs) ? regs[t].max_inst : 1; }

int app_alloc(int t)
{
    if (!app_ensure_loaded(t)) return -1;
    if (t < 0 || t >= nregs) return -1;
    for (int i = 0; i < regs[t].max_inst; i++)
        if (!reg_used[t][i]) {
            reg_used[t][i] = 1;
            if (regs[t].open) {

                volatile int failed = 0;
                int resident = kext_current();
                int cpu_prev = cpu_context(t);
                kext_enter(reg_owner[t]);
                FAULT_GUARD(regs[t].open(i), {
                    failed = 1;
                    klog("open handler faulted - window not opened\n");
                });
                kext_enter(resident);
                cpu_context(cpu_prev);
                if (failed) { app_free(t, i); return -1; }
            }
            return i;
        }
    return -1;
}

void app_free(int t, int inst)
{
    if (t < 0 || t >= nregs || inst < 0 || inst >= MAXINST) return;

    if (reg_used[t][inst] && regs[t].close) {
        int resident = kext_current();
        int cpu_prev = cpu_context(t);
        kext_enter(reg_owner[t]);
        FAULT_GUARD(regs[t].close(inst),
                    klog("close handler faulted - window closed anyway\n"));
        kext_enter(resident); cpu_context(cpu_prev);
    }
    reg_used[t][inst] = 0;
}

int app_type_owner(int t)
{
    return (t >= 0 && t < nregs) ? reg_owner[t] : -1;
}

void app_client_size(int t, int inst, int *w, int *h)
{
    if (t >= 0 && t < nregs) regs[t].client_size(inst, w, h);
    else { *w = 320; *h = 200; }
}

void app_min_client(int t, int *w, int *h)
{
    if (t >= 0 && t < nregs && regs[t].min_client) regs[t].min_client(w, h);
    else app_client_size(t, 0, w, h);
}

int app_resizable(int t) { return t >= 0 && t < nregs && regs[t].resizable; }
int app_live_draw(int t) { return t >= 0 && t < nregs && (regs[t].live_draw & APP_LIVE_DRAW); }

extern void fault_show_banner(const char *msg);
static volatile u8 hang_ended;

static void app_recover(Win *w)
{
    char msg[72];
    const char *who = kext_at(fault_eip);
    const char *nm = w->tbuf_on ? w->tbuf : w->title;
    if (hang_ended) {
        hang_ended = 0;
        kfmt(msg, sizeof msg, "%s stopped responding - ended", nm);
    } else {
        kfmt(msg, sizeof msg, "%s crashed (P%u) - window closed",
             who ? who : nm, fault_vec);
    }
    klog(msg);
    if (!fault_fallback[thr_self]) fault_show_banner(msg);
    int idx = (int)(w - wins);
    if (idx >= 0 && idx < MAXWIN) win_close(idx);
}

void app_draw(Win *w, int cx, int cy, int cw, int ch)
{
    if (w->type >= nregs) return;
    int cpu_prev = cpu_context(w->type);
    kext_enter(reg_owner[w->type]);

    FAULT_GUARD(regs[w->type].draw(w, cx, cy, cw, ch),
                { if (!fault_fallback[thr_self]) app_recover(w); });
    cpu_context(cpu_prev);
}

enum { AE_KEY, AE_MOUSE, AE_WHEEL, AE_CALLBACK, AE_DROP };
typedef struct {void *fn,*ctx;int value,is_path,present;char path[128];} AppCallback;
typedef struct {char type[16],data[4096];int x,y;} AppDrop;
typedef struct { u8 kind, win; int a, b, c, d, e; } AppEv;

static AppEv aq[AQ_SIZE*2];
static int aq_n;
static u32 aq_dropped;
static struct { int active,win,type,owner,legacy,kill,cancel; u32 since,prog,io; } jobs[THR_MAX];
static Mutex buffer_mutex=MUTEX_INIT;
static Mutex network_mutex=MUTEX_INIT;
void app_network_lock(void){mtx_lock(&network_mutex);}
void app_network_unlock(void){mtx_unlock(&network_mutex);}
static int watched_thr=-1;
static volatile int run_win=-1;
volatile u32 app_io_tick;

void app_buffer_lock(void){mtx_lock(&buffer_mutex);}
void app_buffer_unlock(void){mtx_unlock(&buffer_mutex);}
int app_current_window(void){return jobs[thr_self].active?jobs[thr_self].win:-1;}
int app_cancel_pending(void){return jobs[thr_self].active&&jobs[thr_self].cancel;}
void app_cancel_window(int win)
{
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&jobs[t].win==win)jobs[t].cancel=1;
}
int app_job_info(int win,u32 *elapsed,u32 *prog,u32 *io)
{
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&jobs[t].win==win){*elapsed=ticks-jobs[t].since;*prog=jobs[t].prog;*io=jobs[t].io;return 1;}
    *elapsed=*prog=*io=0;return 0;
}
int app_handler_running(int win)
{
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&jobs[t].win==win)return 1;
    return 0;
}
int app_busy(int win)
{
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&jobs[t].win==win&&ticks-jobs[t].since>=BC_SHOW)return 1;
    return 0;
}
void app_note_pump(void){if(jobs[thr_self].active)jobs[thr_self].prog++;}
u32 app_progress(void){return watched_thr<0?0:jobs[watched_thr].prog;}
void app_note_io(void)
{
    if(jobs[thr_self].active){jobs[thr_self].io=ticks;jobs[thr_self].prog++;}
}
int app_stuck(u32 *elapsed)
{
    int best=-1;u32 age=0;
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&& (best<0||ticks-jobs[t].since>age)){best=t;age=ticks-jobs[t].since;}
    watched_thr=best;run_win=best<0?-1:jobs[best].win;app_io_tick=best<0?0:jobs[best].io;
    if(elapsed)*elapsed=age;return run_win;
}
void app_kill_request(int win)
{
    for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&jobs[t].win==win){jobs[t].cancel=1;jobs[t].kill=1;}
}
void app_kill_poll(void)
{
    int t=thr_self;
    if(kupd_critical||!jobs[t].active||!jobs[t].kill||!fault_armed[t])return;
    if(mtx_held_count()>(jobs[t].legacy?1:0))return;
    jobs[t].kill=0;hang_ended=1;Win *w=&wins[jobs[t].win];
    fault_record_hang(w->tbuf_on?w->tbuf:w->title);fault_armed[t]=0;
    in_irq=0;fj_long(&fault_ctx[t],1);
}
u32 app_q_dropped(void){return aq_dropped;}
int app_q_depth(void){return aq_n;}
void app_forget_window(int win)
{
    u32 f=irq_save();
    for(int i=0;i<aq_n;)if(aq[i].win==win){if(aq[i].kind==AE_CALLBACK||aq[i].kind==AE_DROP)kfree((void *)aq[i].a);memmove(aq+i,aq+i+1,(--aq_n-i)*sizeof *aq);}else i++;
    irq_restore(f);
}
static int aq_post(u8 kind,int win,int a,int b,int c,int d,int e)
{
    if(win<0||win>=MAXWIN)return 0;
    u32 f=irq_save();
    int queued=0;
    for(int i=0;i<aq_n;i++)if(aq[i].win==win)queued++;
    if(kind==AE_MOUSE&&c==EV_DRAG&&aq_n&&aq[aq_n-1].win==win&&aq[aq_n-1].kind==AE_MOUSE&&aq[aq_n-1].c==EV_DRAG){
        AppEv *q=&aq[aq_n-1];q->a=a;q->b=b;q->d=d;q->e=e;irq_restore(f);return 1;
    }
    if(aq_n>=AQ_SIZE*2||(queued>=AQ_SIZE+16&&!(kind==AE_MOUSE&&c==EV_RELEASE))){
        if(kind==AE_MOUSE&&c==EV_RELEASE){
            for(int i=aq_n-1;i>=0;i--)if(aq[i].win==win&&aq[i].kind==AE_MOUSE&&aq[i].c==EV_DRAG){aq[i].a=a;aq[i].b=b;aq[i].c=c;aq[i].d=d;aq[i].e=e;irq_restore(f);return 1;}
        }
        aq_dropped++;irq_restore(f);return 0;
    }
    AppEv *q=&aq[aq_n++];q->kind=kind;q->win=win;q->a=a;q->b=b;q->c=c;q->d=d;q->e=e;
    irq_restore(f);return 1;
}
static int aq_take(AppEv *ev)
{
    u32 f=irq_save();
    for(int i=0;i<aq_n;i++){
        Win *w=&wins[aq[i].win];if(!w->used)continue;
        int owner=reg_owner[w->type],legacy=!(regs[w->type].live_draw&APP_INDEPENDENT),blocked=0;
        for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&
            (jobs[t].type==w->type||(owner>=0&&jobs[t].owner==owner)||(legacy&&jobs[t].legacy)))blocked=1;
        if(blocked||(legacy&&buffer_mutex.held))continue;
        *ev=aq[i];memmove(aq+i,aq+i+1,(--aq_n-i)*sizeof *aq);
        int t=thr_self;jobs[t].active=1;jobs[t].win=ev->win;jobs[t].type=w->type;jobs[t].owner=owner;
        jobs[t].legacy=legacy;jobs[t].since=ticks;jobs[t].prog=0;jobs[t].io=0;jobs[t].kill=jobs[t].cancel=0;
        if(legacy)app_buffer_lock();irq_restore(f);return 1;
    }
    irq_restore(f);return 0;
}

int app_owner_busy(int owner)
{
    if(owner<0)return 0;
    for(int t=0;t<THR_MAX;t++)if(t!=thr_self&&jobs[t].active&&jobs[t].owner==owner)return 1;
    return 0;
}
int app_callback(int owner,int win,void *fn,void *ctx,int value,const char *path,int is_path)
{
    if(!fn||owner<0||win<0)return 0;
    if(win>=MAXWIN||!wins[win].used||reg_owner[wins[win].type]!=owner)return 1;
    AppCallback *p=kmalloc(sizeof *p);
    if(!p){fault_show_banner("E42 - Operation could not be queued.");return 1;}
    p->fn=fn;p->ctx=ctx;p->value=value;p->is_path=is_path;p->present=path!=0;strlcpy(p->path,path?path:"",sizeof p->path);
    if(!aq_post(AE_CALLBACK,win,(int)p,0,0,0,0)){kfree(p);fault_show_banner("E42 - Input queue is full.");}
    return 1;
}

#define HANDLER_GUARD(body, w) do {                                   \
    int _pd = preempt_depth();                                        \
    FAULT_GUARD(body, ({ worker_unwind(_pd); app_recover(w); }));     \
} while (0)

static void app_mouse_now(Win *w, int lx, int ly, int ev, int cw, int ch)
{
    if (w->type >= nregs || !regs[w->type].mouse) return;
    int cpu_prev = cpu_context(w->type);
    kext_enter(reg_owner[w->type]);
    HANDLER_GUARD(regs[w->type].mouse(w->inst, lx, ly, ev, cw, ch), w);
    cpu_context(cpu_prev);
}

static void app_key_now(Win *w, int k)
{
    if (w->type >= nregs || !regs[w->type].key) return;
    int cpu_prev = cpu_context(w->type);
    kext_enter(reg_owner[w->type]);
    HANDLER_GUARD(regs[w->type].key(w->inst, k), w);
    cpu_context(cpu_prev);
}

static void app_wheel_now(Win *w, int dz)
{
    if (w->type >= nregs || !regs[w->type].wheel) return;
    int cpu_prev = cpu_context(w->type);
    kext_enter(reg_owner[w->type]);
    HANDLER_GUARD(regs[w->type].wheel(w->inst, dz), w);
    cpu_context(cpu_prev);
}

static void app_job_now(Win *w,AppEv *ev)
{
    int prev=cpu_context(w->type);kext_enter(reg_owner[w->type]);
    if(ev->kind==AE_CALLBACK){
        AppCallback *p=(AppCallback *)ev->a;
        HANDLER_GUARD({if(p->is_path)((void (*)(const char *,void *))p->fn)(p->present?p->path:0,p->ctx);else ((void (*)(int,void *))p->fn)(p->value,p->ctx);},w);
    }else{
        AppDrop *p=(AppDrop *)ev->a;
        if(regs[w->type].drop)HANDLER_GUARD(regs[w->type].drop(w->inst,p->x,p->y,p->type,p->data),w);
    }
    kfree((void *)ev->a);cpu_context(prev);
}
void app_worker(void)
{
    for(;;){
        AppEv ev;
        if(!aq_take(&ev)){thr_yield();continue;}
        Win *w=&wins[ev.win];
        if(ev.kind==AE_KEY)app_key_now(w,ev.a);
        else if(ev.kind==AE_WHEEL)app_wheel_now(w,ev.a);
        else if(ev.kind==AE_MOUSE)app_mouse_now(w,ev.a,ev.b,ev.c,ev.d,ev.e);
        else app_job_now(w,&ev);
        kext_enter(-1);
        app_local_progress(0,0,-1);
        if(jobs[thr_self].legacy)app_buffer_unlock();
        jobs[thr_self].active=0;
        win_close_flush();gui_dirty=1;
    }
}

void app_mouse(Win *w, int lx, int ly, int ev, int cw, int ch)
{
    aq_post(AE_MOUSE, (int)(w - wins), lx, ly, ev, cw, ch);
}

void app_key(Win *w, int k)
{
    aq_post(AE_KEY, (int)(w - wins), k, 0, 0, 0, 0);
}

void app_wheel(Win *w, int dz)
{
    aq_post(AE_WHEEL, (int)(w - wins), dz, 0, 0, 0, 0);
}

void app_drop(Win *w,int lx,int ly,const char *type,const char *data)
{
    if(w->type>=nregs||!regs[w->type].drop||!type||!data)return;
    if(strlen(type)>=16||strlen(data)>=4096)return;
    AppDrop *p=kmalloc(sizeof *p);if(!p){fault_show_banner("E42 - File drop could not be queued.");return;}
    p->x=lx;p->y=ly;strlcpy(p->type,type,sizeof p->type);strlcpy(p->data,data,sizeof p->data);
    if(!aq_post(AE_DROP,w-wins,(int)p,0,0,0,0)){kfree(p);fault_show_banner("E42 - Input queue is full.");}
}

static void term_csize(int inst, int *w, int *h)
{ (void)inst; *w = tdcols * 8 + 8; *h = tdrows * 16 + 8; }
static void term_min(int *w, int *h) { *w = 30 * 8 + 8; *h = 6 * 16 + 8; }
static void sysinfo_csize(int inst, int *w, int *h)
{ (void)inst; *w = 438; *h = 214; }
static void sysinfo_draw_w(Win *w, int cx, int cy, int cw, int ch)
{ (void)cw; (void)ch; sysinfo_draw(w, cx, cy); }

void apps_init(void)
{
    tdcols = (SW - 60) / 8;
    if (tdcols > TCMAX) tdcols = TCMAX;
    if (tdcols < 30) tdcols = 30;
    tdrows = (SH - 120) / 16;
    if (tdrows > 40) tdrows = 40;
    if (tdrows < 5) tdrows = 5;

    static const AppDesc dterm = {
        .title = "Terminal", .max_inst = MAXINST, .resizable = 1, .in_menu = 1,
        .open = term_reset, .draw = term_draw, .key = term_key,
        .wheel = term_wheel, .client_size = term_csize, .min_client = term_min, .drop=term_drop,

        .live_draw = 1,
    };
    static const AppDesc dsys = {
        .title = "System Info", .max_inst = 1, .in_menu = 1,
        .draw = sysinfo_draw_w, .client_size = sysinfo_csize,
    };
    register_app(&dterm);
    register_app(&dsys);
}
