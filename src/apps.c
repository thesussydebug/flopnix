/* Built-in shell commands and terminal output. */
#include "os.h"
#include "debug.h"
#include "shpath.h"
#include "../kexts/net_wire.inc"
#include "ramtest.inc"
#include "appq.inc"
#include "busycore.inc"
#include "brkkey.inc"
#include "shcwd.inc"
#include "shellstream.h"
#include "shellterm.h"

extern char __bss_start[], __bss_end[], __load_end[];

int net_parse_ip(const char *s, u32 *out);
static void sh_dispatch(char *cmd);

static ShellStream *streams[THR_MAX];
static u8 in_shell_exec;
#define STREAM streams[thr_self]

static const ShellTermOps *tops(void)
{
    const ShellTermOps *o = service_get("shell.term");
    return o && o->abi == SHELL_TERM_ABI ? o : 0;
}
static void tprint(const char *s)
{
    ShellStream *st = STREAM;
    if (st) { while (*s) st->putc(*s++, st->ctx); return; }
    const ShellTermOps *o = tops();
    if (o) o->print(s);
}
static void tputc(char c) { char s[2] = { c, 0 }; tprint(s); }

const char *shell_cwd_get(void) { return STREAM ? STREAM->cwd : ""; }

int shell_cwd_set(const char *d)
{
    if (!STREAM || !d || (int)strlen(d) >= SC_MAX) return 0;
    strlcpy(STREAM->cwd, d, SC_MAX);
    return 1;
}

u32 used_kb(void)
{

    u32 img = (u32)__load_end - 0x8000;
    u32 bss = (u32)__bss_end - (u32)__bss_start;
    return (img + bss + IOBUF_SZ + (u32)(SW * SH) + 1023 +
            kapi.mem_info(MI_ARENA_RO) + kapi.mem_info(MI_ARENA_RW) +
            kapi.mem_info(MI_POOL_USED) + heap_capacity() - heap_free()) / 1024 + 120;
}

void shell_print(const char *s) { tprint(s); }

void term_clear(void) { tprint("\033[2J\033[H"); }
int term_fx(int mode) { const ShellTermOps *o = tops(); return o ? o->fx(mode) : 0; }
const char *term_hist(int i) { const ShellTermOps *o = tops(); return o ? o->hist(i) : 0; }
int shell_win_close(int i)
{
    if (i < 0 || i >= MAXWIN || !wins[i].used) return 0;
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

static AppDesc regs[MAX_APPS];

static int reg_owner[MAX_APPS];
static u8 reg_used[MAX_APPS][MAXINST];
static int nregs;

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

static int shell_stream_run(ShellStream *stream,const char *line)
{
    if(!stream||!stream->putc||!line||strlen(line)>=192)return -1;
    u32 flags=irq_save();
    if(in_shell_exec){irq_restore(flags);return -2;}
    in_shell_exec=1;STREAM=stream;irq_restore(flags);
    char buf[192];strlcpy(buf,line,sizeof buf);
    int resident=kext_current(),pd=preempt_depth();
    volatile int result=0;
    FAULT_GUARD(sh_dispatch(buf),{worker_unwind(pd);result=-3;});
    kext_enter(resident);STREAM=0;in_shell_exec=0;
    return result;
}
const ShellStreamOps shell_stream_ops={SHELL_STREAM_ABI,shell_stream_run};

static void null_putc(char c, void *ctx) { (void)c; (void)ctx; }
int shell_exec(const char *line)
{
    if (!line || !line[0]) return -1;
    const ShellTermOps *o = tops();
    if (o) return o->exec(line);
    static ShellStream quiet = { "", null_putc, 0 };
    return shell_stream_run(&quiet, line);
}
int shell_history_count(void) { int n = 0; while (term_hist(n)) n++; return n; }
const char *shell_history(int i) { const char *h = term_hist(i); return h ? h : ""; }

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
    if (t >= 0 && t < nregs) {
        int resident = kext_current();
        kext_enter(reg_owner[t]);
        regs[t].client_size(inst, w, h);
        kext_enter(resident);
    }
    else { *w = 320; *h = 200; }
}

void app_min_client(int t, int *w, int *h)
{
    if (t >= 0 && t < nregs && regs[t].min_client) {
        int resident = kext_current();
        kext_enter(reg_owner[t]);
        regs[t].min_client(w, h);
        kext_enter(resident);
    }
    else app_client_size(t, 0, w, h);
}

int app_resizable(int t) { return t >= 0 && t < nregs && regs[t].resizable; }
int app_live_draw(int t) { return app_draw_flags(t) & APP_LIVE_DRAW; }
int app_draw_flags(int t) { return t >= 0 && t < nregs ? regs[t].live_draw : 0; }

extern void fault_show_banner(const char *msg);

static void app_recover(Win *w)
{
    char msg[128];
    const FaultRec *fault = fault_get(0);
    const char *nm = w->tbuf_on ? w->tbuf : w->title;
    if (fault_vec == FAULT_VEC_HANG) {
        kfmt(msg, sizeof msg, "%s stopped responding - ended", nm);
    } else {
        kfmt(msg, sizeof msg, "P%u %s - window closed",
             fault_vec, fault ? fault->location : nm);
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

enum { AE_KEY, AE_MOUSE, AE_WHEEL, AE_CALLBACK, AE_DROP, AE_WORK };
typedef struct {void *fn,*ctx;int value,is_path,present;char path[128];} AppCallback;
typedef struct {char type[16],data[4096];int x,y;} AppDrop;
typedef struct { u8 kind, win; int a, b, c, d, e; } AppEv;

static AppEv aq[AQ_SIZE*2];
static int aq_n;
static u32 aq_dropped,aq_peak;
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
    if(kupd_critical||!jobs[t].active||!jobs[t].kill||!fault_armed[t]||gui_pumping())return;
    if(mtx_held_count()>(jobs[t].legacy?1:0))return;
    jobs[t].kill=0;Win *w=&wins[jobs[t].win];
    fault_record_hang(w->tbuf_on?w->tbuf:w->title);fault_armed[t]=0;
    in_irq=0;fj_long(&fault_ctx[t],1);
}
static u8 brk_beep;
void app_break_poll(void)
{
    if (brk_beep && !--brk_beep) speaker_off();
    if (!brk_req) return;
    BrkIn b = { em_age, brk_since, brk_req, thr_self == 0, (u8)fault_armed[0], (u8)mtx_held_count(),
                (u8)usb_held_by_self(), (u8)gui_pumping() };
    int d = brk_decide(&b);
    if (d == BRK_WAIT) return;
    brk_req = 0;
    u32 eip = panic_frame[10];
    if (d == BRK_EMERGENCY) emergency_enter(0xffffffffu, 0, eip, 0, EM_BREAK);
    if (d == BRK_REFUSE) { speaker_tone(BRK_BEEP_HZ); brk_beep = BRK_BEEP_TICKS; }
    if (d != BRK_SOFT) return;
    const KextInfo *k = kext_get(kext_current());
    fault_record_hang(k ? path_base(k->name) : "kernel");
    FaultRec *r = (FaultRec *)fault_get(0);
    r->eip = eip;
    fault_symbol(eip, r->location, sizeof r->location);
    char msg[80];
    kfmt(msg, sizeof msg, "%s stopped with Pause", r->owner);
    klog(msg); klog("\n"); fault_show_banner(msg);
    input_mute();
    busy_end();
    fault_armed[0] = 0; in_irq = 0; fj_long(&fault_ctx[0], 1);
}
u32 app_q_dropped(void){return aq_dropped;}
int app_q_depth(void){return aq_n;}
u32 app_q_peak(void){return aq_peak;}
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
    if((u32)aq_n>=aq_peak)aq_peak=aq_n+1;
    AppEv *q=&aq[aq_n++];q->kind=kind;q->win=win;q->a=a;q->b=b;q->c=c;q->d=d;q->e=e;
    irq_restore(f);return 1;
}
static int aq_who(const AppEv *e,int *type,int *owner,int *legacy)
{
    *type=-1;*owner=e->c;*legacy=0;
    if(e->kind==AE_WORK)return 1;
    Win *w=&wins[e->win];if(!w->used)return 0;
    *type=w->type;*owner=reg_owner[w->type];*legacy=!(regs[w->type].live_draw&APP_INDEPENDENT);
    return 1;
}
static int aq_find(void)
{
    for(int i=0;i<aq_n;i++){
        int type,owner,legacy,blocked=0;
        if(!aq_who(&aq[i],&type,&owner,&legacy))continue;
        for(int t=0;t<THR_MAX;t++)if(jobs[t].active&&
            aq_conflict(type,owner,legacy,jobs[t].type,jobs[t].owner,jobs[t].legacy))blocked=1;
        if(!blocked&&!kext_timer_busy(owner)&&!(legacy&&buffer_mutex.held))return i;
    }
    return -1;
}
int app_q_ready(void){u32 f=irq_save();int r=aq_find()>=0;irq_restore(f);return r;}
static int aq_take(AppEv *ev)
{
    u32 f=irq_save();
    int i=aq_find();
    if(i<0){irq_restore(f);return 0;}
    int type,owner,legacy;
    aq_who(&aq[i],&type,&owner,&legacy);
    *ev=aq[i];memmove(aq+i,aq+i+1,(--aq_n-i)*sizeof *aq);
    int t=thr_self;jobs[t].active=1;jobs[t].win=type<0?-1:ev->win;jobs[t].type=type;jobs[t].owner=owner;
    jobs[t].legacy=legacy;jobs[t].since=ticks;jobs[t].prog=0;jobs[t].io=0;jobs[t].kill=jobs[t].cancel=0;
    if(legacy)app_buffer_lock();irq_restore(f);return 1;
}

int app_work_post(int owner,void (*fn)(void *),void *ctx)
{
    if(!fn||owner<0)return -1;
    u32 f=irq_save();
    for(int i=0;i<aq_n;i++)if(aq[i].kind==AE_WORK&&aq[i].a==(int)fn&&aq[i].b==(int)ctx&&aq[i].c==owner){irq_restore(f);return 0;}
    if(aq_n>=AQ_SIZE*2){aq_dropped++;irq_restore(f);return -1;}
    AppEv *q=&aq[aq_n++];q->kind=AE_WORK;q->win=AQ_WORK;q->a=(int)fn;q->b=(int)ctx;q->c=owner;
    irq_restore(f);return 0;
}
int app_work_queued(int owner,int drop)
{
    u32 f=irq_save();int n=0;
    for(int i=0;i<aq_n;)if(aq[i].kind==AE_WORK&&aq[i].c==owner){n++;if(!drop){i++;continue;}memmove(aq+i,aq+i+1,(--aq_n-i)*sizeof *aq);}else i++;
    irq_restore(f);return n;
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
static void app_work_now(AppEv *ev)
{
    int prev=cpu_context(app_type_owned(ev->c)),pd=preempt_depth();kext_enter(ev->c);
    FAULT_GUARD(((void (*)(void *))ev->a)((void *)ev->b),({
        worker_unwind(pd);
        char msg[96];const FaultRec *fault=fault_get(0);
        kfmt(msg,sizeof msg,"P%u %s - work stopped",fault_vec,fault?fault->location:"unknown");
        klog(msg);
        if(!fault_fallback[thr_self])fault_show_banner(msg);
    }));
    cpu_context(prev);
}
void app_worker(void)
{
    for(;;){
        AppEv ev;
        if(!aq_take(&ev)){thr_yield();continue;}
        Win *w=ev.kind==AE_WORK?0:&wins[ev.win];
        if(!w)app_work_now(&ev);
        else if(ev.kind==AE_KEY)app_key_now(w,ev.a);
        else if(ev.kind==AE_WHEEL)app_wheel_now(w,ev.a);
        else if(ev.kind==AE_MOUSE)app_mouse_now(w,ev.a,ev.b,ev.c,ev.d,ev.e);
        else app_job_now(w,&ev);
        debug_event(DBG_SLOW,w?regs[jobs[thr_self].type].title:"work",
            ticks-jobs[thr_self].since,ev.kind,ev.win);
        kext_enter(-1);
        app_local_progress(0,0,-1);
        if(jobs[thr_self].legacy)app_buffer_unlock();
        jobs[thr_self].active=0;
        win_close_flush();
        if(w&&w->used)win_redraw(w->type,w->inst);
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
