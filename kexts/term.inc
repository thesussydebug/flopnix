/* Terminal window: scrollback, input line and history. Commands run through shell.stream. */
#include "tabcomp.inc"
#include "clipline.inc"
#include "shellstream.h"
#include "shellterm.h"

#define TCMAX 80
#define SBMAX 160
#define THIST 12
#define TBOOT_HANG 0x80

typedef struct {
    char (*sb)[TCMAX];
    int  head, nlines, cx, view, cols, rows;
    u8   rainbow, fx, running, esc, tab_on;
    char line[192];
    char pending[192];
    int  len, inpx;
    char (*hist)[192];
    int  hist_n, hist_pos;
    char tab_pre[32];
    int  tab_idx;
    ShellStream st;
} Term;

static Term terms[MAXINST];
static Term *term_last;
static int term_type = -1, tdcols, tdrows;
static const ShellStreamOps *term_sso;

static char *line_at(Term *t, int i) { return t->sb[(t->head + i) % SBMAX]; }
static char *cur_line(Term *t) { return line_at(t, t->nlines - 1); }
static void t_dirty(Term *t) { api->win_redraw(term_type, (int)(t - terms)); }

static Term *t_current(void)
{
    const char *cwd = api->shell_cwd();
    for (int i = 0; i < MAXINST; i++)
        if (cwd == terms[i].st.cwd) return &terms[i];
    return 0;
}

static void t_nl(Term *t)
{
    t->cx = 0;
    if (t->nlines < SBMAX) t->nlines++;
    else t->head = (t->head + 1) % SBMAX;
    memset(cur_line(t), 0, TCMAX);
}

static void t_putc(char c, void *ctx)
{
    Term *t = ctx;
    if (!t->sb) return;
    t->view = 0;
    t_dirty(t);
    if (c == 27) { t->esc = 1; return; }
    if (t->esc) {
        if ((c | 32) >= 'a' && (c | 32) <= 'z') {
            t->esc = 0;
            if (c == 'J') {
                memset(t->sb, 0, SBMAX * TCMAX);
                t->head = t->cx = 0;
                t->nlines = 1;
            }
        }
        return;
    }
    if (c == '\n') { t_nl(t); return; }
    if (c == '\b') { if (t->cx > 0) { t->cx--; cur_line(t)[t->cx] = 0; } return; }
    if (t->cx >= TCMAX - 1) t_nl(t);
    cur_line(t)[t->cx] = c;
    if (++t->cx >= t->cols) t_nl(t);
}
static void t_print(Term *t, const char *s) { while (*s) t_putc(*s++, t); }

static void t_input(Term *t)
{
    char *row = cur_line(t);
    int avail = t->cols - t->inpx - 1;
    if (avail < 1) avail = 1;
    int start = 0, col = t->inpx;
    if (t->len > avail) {
        start = t->len - avail + 1;
        row[col++] = '<';
    }
    while (col < t->cols - 1 && t->line[start]) row[col++] = t->line[start++];
    for (int j = col; j < TCMAX; j++) row[j] = 0;
    t->cx = col < TCMAX ? col : TCMAX - 1;
    t->view = 0;
    t_dirty(t);
}

static void t_prompt(Term *t)
{
    char p[128];
    kfmt(p, sizeof p, "root@flopnix:/%s# ", t->st.cwd);
    t_print(t, p);
    t->len = 0;
    t->line[0] = 0;
    t->inpx = t->cx;
    t->running = 0;
    if (t->pending[0]) {
        strlcpy(t->line, t->pending, sizeof t->line);
        t->len = strlen(t->line);
        t->pending[0] = 0;
        t_input(t);
    }
}

static void t_set_input(Term *t, const char *s)
{
    t->len = 0;
    for (const char *p = s; *p && t->len < (int)sizeof t->line - 1; p++) t->line[t->len++] = *p;
    t->line[t->len] = 0;
    t_input(t);
}

static void hist_push(Term *t, const char *s)
{
    if (!s[0]) return;
    if (t->hist_n > 0 && !strcmp(t->hist[(t->hist_n - 1) % THIST], s)) { t->hist_pos = t->hist_n; return; }
    strlcpy(t->hist[t->hist_n % THIST], s, sizeof t->hist[0]);
    t->hist_n++;
    t->hist_pos = t->hist_n;
}

static void hist_recall(Term *t, int dir)
{
    int lo = t->hist_n > THIST ? t->hist_n - THIST : 0;
    int np = t->hist_pos + dir;
    if (np < lo) np = lo;
    if (np > t->hist_n) np = t->hist_n;
    t->hist_pos = np;
    t_set_input(t, np == t->hist_n ? "" : t->hist[np % THIST]);
}

static const char *t_hist(int i)
{
    Term *t = t_current();
    if (!t) t = term_last;
    if (!t || !t->hist || i < 0) return 0;
    int count = t->hist_n > THIST ? THIST : t->hist_n;
    if (i >= count) return 0;
    int lo = t->hist_n > THIST ? t->hist_n - THIST : 0;
    return t->hist[(lo + i) % THIST];
}

static int t_fx(int mode)
{
    Term *t = t_current();
    if (!t || !t->sb) return 0;
    if (mode == 1) return t->fx = 1;
    if (mode == 2) return t->rainbow = !t->rainbow;
    return 0;
}

static void t_run(Term *t)
{
    t_putc('\n', t);
    t->line[t->len] = 0;
    hist_push(t, t->line);
    char command[sizeof t->line];
    strlcpy(command, t->line, sizeof command);
    t->running = 1;
    int r = command[0] && term_sso ? term_sso->run(&t->st, command) : 0;
    if (r == -2) t_print(t, "Another command is running (terminal or remote); try again.\n");
    else if (r == -3) t_print(t, "The command faulted and was stopped.\n");
    t->running = 0;
    if (!t->fx) t_prompt(t);
}

static void t_scroll(Term *t, int lines)
{
    if (!t->sb) return;
    int maxv = t->nlines - t->rows;
    if (maxv < 0) maxv = 0;
    t->view += lines;
    if (t->view < 0) t->view = 0;
    if (t->view > maxv) t->view = maxv;
}

static void t_tab(Term *t)
{
    for (int i = 0; i < t->len; i++)
        if (t->line[i] == ' ') return;
    if (!t->tab_on) {
        int n = t->len < (int)sizeof t->tab_pre - 1 ? t->len : (int)sizeof t->tab_pre - 1;
        for (int i = 0; i < n; i++) t->tab_pre[i] = t->line[i];
        t->tab_pre[n] = 0;
        t->tab_idx = 0;
        t->tab_on = 1;
    }
    int m = tc_count(t->tab_pre, sh_names, SH_NNAMES);
    if (!m) { t->tab_on = 0; return; }
    const char *c = tc_nth(t->tab_pre, sh_names, SH_NNAMES, t->tab_idx % m);
    t->tab_idx++;
    if (!c) return;
    while (t->len > 0) { t->len--; t_putc('\b', t); }
    for (int i = 0; c[i] && t->len < (int)sizeof t->line - 1; i++) {
        t->line[t->len++] = c[i];
        t_putc(c[i], t);
    }
    if (m == 1) {
        if (t->len < (int)sizeof t->line - 1) { t->line[t->len++] = ' '; t_putc(' ', t); }
        t->tab_on = 0;
    }
}

static void t_copy(Term *t)
{
    char *out = api->kmalloc(4096);
    if (!out) return;
    int o = 0;
    int first = t->nlines - t->rows - t->view;
    if (first < 0) first = 0;
    int last = t->nlines - t->view;
    for (int i = first; i < last; i++) {
        const char *l = line_at(t, i);
        for (int x = 0; l[x] && x < TCMAX && o < 4096 - 2; x++) out[o++] = l[x];
        if (o < 4096 - 1) out[o++] = '\n';
    }
    out[o] = 0;
    api->clip_set_text(out);
    api->kfree(out);
}

static void t_key(int inst, int k)
{
    Term *t = &terms[inst];
    term_last = t;
    if (!t->sb) return;
    if (t->fx) { t->fx = 0; t_prompt(t); return; }
    if (k == K_UP)   { hist_recall(t, -1); return; }
    if (k == K_DOWN) { hist_recall(t, 1); return; }
    if (k == K_PGUP) { t_scroll(t, t->rows - 1); return; }
    if (k == K_PGDN) { t_scroll(t, -(t->rows - 1)); return; }
    if (k >= 0x100) return;
    char ch = (char)k;
    if (ch == '\t') { t_tab(t); return; }
    t->tab_on = 0;
    if (ch == '\n') { t_run(t); return; }
    if (ch == '\b') {
        if (t->len > 0) { t->line[--t->len] = 0; t_input(t); }
        return;
    }
    if (ch == 0x16) {
        char clip[192];
        if (api->clip_get_text(clip, sizeof clip) > 0) {
            t->len = cl_paste(t->line, t->len, sizeof t->line, clip);
            t_input(t);
        }
        return;
    }
    if (ch == 0x03) { t_copy(t); return; }
    if (ch < 32) return;
    if (t->len < (int)sizeof t->line - 1) {
        t->line[t->len++] = ch;
        t->line[t->len] = 0;
        t_input(t);
    }
}

static const u8 rain_hue[6] = { C_RED, C_YELLOW, C_BGREEN, C_CYAN, C_BBLUE, C_MAGENTA };

static void t_matrix(int cx, int cy, int cols, int rows)
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
            api->draw_char(cx + 4 + c * 8, cy + 4 + r * 16, g, col);
        }
    }
}

static void t_draw(Win *w, int cx, int cy, int cw, int ch)
{
    Term *t = &terms[w->inst];
    fill_rect(cx, cy, cw, ch, C_TERMBG);
    if (!t->sb) { api->draw_text_clip(cx + 4, cy + 4, "Not enough memory. Close and reopen Terminal.", C_RED, cw - 8); return; }
    int cols = (cw - 8) / 8, rows = (ch - 8) / 16;
    if (cols > TCMAX) cols = TCMAX;
    if (cols < 8) cols = 8;
    if (rows > 44) rows = 44;
    if (rows < 3) rows = 3;
    t->cols = cols; t->rows = rows;
    if (t->fx) { t_matrix(cx, cy, cols, rows); return; }
    int maxv = t->nlines - rows;
    if (maxv < 0) maxv = 0;
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
            api->draw_char(cx + 4 + c * 8, cy + 4 + r * 16, ln[c], col);
        }
    }
    if (t->view > 0) {
        char tag[20];
        kfmt(tag, sizeof tag, "-- %d up --", t->view);
        draw_text(cx + cw - (int)strlen(tag) * 8 - 8, cy + 2, tag, C_YELLOW);
    }
    int crow = (t->nlines - 1) - firstvis;
    if (api->win_is_focused(w) && *api->gui_blink && t->view == 0 && crow >= 0 && crow < rows && t->cx < cols)
        fill_rect(cx + 4 + t->cx * 8, cy + 4 + crow * 16 + 14, 8, 2, C_TERMFG);
}

static void t_open(int inst)
{
    Term *t = &terms[inst];
    void *storage = t->sb;
    memset(t, 0, sizeof *t);
    term_last = t;
    t->sb = storage ? storage : api->kmalloc(SBMAX * TCMAX + THIST * 192);
    if (!t->sb) return;
    t->hist = (void *)(t->sb + SBMAX);
    memset(t->sb, 0, SBMAX * TCMAX + THIST * 192);
    api->mem_track("Terminal", t->sb, SBMAX * TCMAX + THIST * 192);
    t->st.putc = t_putc;
    t->st.ctx = t;
    t->nlines = 1;
    t->cols = tdcols;
    t->rows = tdrows;
    t_print(t, OS_NAME " ");
    t_print(t, OS_VER);
    t_print(t, " build ");
    t_print(t, api->os_build_num);
    t_print(t, "\ntype 'help' for commands\n");
    if (api->boot_info(BI_BOOT_DIAG) & TBOOT_HANG) t_print(t, "boot: recovered from a BIOS hang - see 'dmesg'\n");
    t_print(t, "\n");
    t_prompt(t);
}

static void t_close(int inst)
{
    Term *t = &terms[inst];
    api->kfree(t->sb);
    memset(t, 0, sizeof *t);
    if (term_last == t) term_last = 0;
}

static void t_wheel(int inst, int dz) { t_scroll(&terms[inst], dz * 3); }

static void t_drop(int inst, int x, int y, const char *type, const char *data)
{
    (void)x; (void)y;
    Term *t = &terms[inst];
    if (!t->sb || !type || !data || (strcmp(type, "file") && strcmp(type, "file.cut"))) return;
    api->win_focus(term_type, inst);
    if (t->running) sp_insert(t->pending, strlen(t->pending), sizeof t->pending, data);
    else { t->len = sp_insert(t->line, t->len, sizeof t->line, data); t->tab_on = 0; t_input(t); }
}

static void t_csize(int inst, int *w, int *h) { (void)inst; *w = tdcols * 8 + 8; *h = tdrows * 16 + 8; }
static void t_min(int *w, int *h) { *w = 30 * 8 + 8; *h = 6 * 16 + 8; }

static int t_exec(const char *line)
{
    Term *t = term_last && term_last->sb ? term_last : 0;
    for (int i = MAXINST - 1; i >= 0 && !t; i--) if (terms[i].sb) t = &terms[i];
    if (!t) {
        int inst = win_open(term_type);
        if (inst < 0 || inst >= MAXINST || !terms[inst].sb) return -1;
        t = &terms[inst];
    }
    if (t->running || t->fx) return -1;
    t_set_input(t, line);
    t_run(t);
    return 0;
}

static void t_print_last(const char *s) { if (term_last && term_last->sb) t_print(term_last, s); }

static void t_anim(void *ctx)
{
    (void)ctx;
    static u8 odd;
    odd ^= 1;
    for (int i = 0; i < MAXINST; i++)
        if (terms[i].sb && (terms[i].fx || (terms[i].rainbow && odd))) t_dirty(&terms[i]);
}

static const ShellTermOps term_ops = { SHELL_TERM_ABI, t_exec, t_print_last, t_fx, t_hist };

static void term_init(void)
{
    int sw = *api->screen_w, sh = *api->screen_h;
    tdcols = (sw - 60) / 8;
    if (tdcols > TCMAX) tdcols = TCMAX;
    if (tdcols < 30) tdcols = 30;
    tdrows = (sh - 120) / 16;
    if (tdrows > 40) tdrows = 40;
    if (tdrows < 5) tdrows = 5;
    term_sso = api->service_get("shell.stream");
    static const AppDesc term_desc = {
        .title = "Terminal", .max_inst = MAXINST, .resizable = 1, .in_menu = 1,
        .open = t_open, .close = t_close, .draw = t_draw, .key = t_key,
        .wheel = t_wheel, .client_size = t_csize, .min_client = t_min, .drop = t_drop,
        .live_draw = APP_LIVE_DRAW | APP_POINTER_FREE,
    };
    term_type = api->register_app(&term_desc);
    if (term_type < 0) return;
    api->register_service("shell.term", &term_ops);
    api->timer_add(3, t_anim, 0);
}
