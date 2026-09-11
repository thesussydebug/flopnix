#include "kapi.h"
#include "shpath.h"
#include "clipline.inc"
#include "gdi.h"

#include "ui.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int edit_type = -1;

#define kfmt            api->kfmt
#define strlen          api->strlen
#define strcmp          api->strcmp
#define strncmp         api->strncmp
#define strcasecmp      api->strcasecmp
#define strlcpy         api->strlcpy
#define memcpy          api->memcpy
#define memmove         api->memmove
#define memset          api->memset
#define human_size      api->human_size
#define human_size_kb   api->human_size_kb
#define ticks           (*api->ticks)
#define timer_alive     (*api->timer_alive)
#define rtc_read        api->rtc_read
#define rtc_now_dos     api->rtc_now_dos
#define dos_fmt         api->dos_fmt
#define SW              (*api->screen_w)
#define SH              (*api->screen_h)
#define mx              (*api->mouse_x)
#define my              (*api->mouse_y)
#define gui_blink       (*api->gui_blink)
#define fill_rect       api->fill_rect
#define hline           api->hline
#define vline           api->vline
#define bevel           api->bevel
#define panel           api->panel
#define draw_char       api->draw_char
#define draw_text       api->draw_text
#define draw_text_clip  api->draw_text_clip
#define draw_text_clip2 api->draw_text_clip2
#define draw_sbar       api->draw_sbar
#define sbar_from_pos   api->sbar_from_pos
#define focus_rect      api->focus_rect
#define blit            api->blit
#define palette_rgb     api->palette_rgb
#define palette_nearest api->palette_nearest
#define win_open        api->win_open
#define win_fit_client  api->win_fit_client
#define win_is_focused  api->win_is_focused
#define fs_ensure()     (fs_slot(0) != 0)
#define fs_slot         api->fs_slot
#define fs_read         api->fs_read
#define fs_write        api->fs_write
#define fs_delete       api->fs_delete
#define fs_exists       api->fs_exists
#define fs_free_kb      api->fs_free_kb
#define ext_type        api->ext_type
#define fat_mount       api->fat_mount
#define fat_list        api->fat_list
#define fat_read        api->fat_read
#define fat_write       api->fat_write
#define fat_delete      api->fat_delete
#define fat_writable    api->fat_writable
#define fat_label       api->fat_label
#define fat_total_kb    api->fat_total_kb
#define fat_free_kb     api->fat_free_kb
#define usb_present     api->usb_present
#define usb_read        api->usb_read
#define usb_write       api->usb_write
#define usb_capacity_kb api->usb_capacity_kb
#define usb_capacity_sectors api->usb_capacity_sectors
#define usb_model       api->usb_model
#define iobuf           api->iobuf
#define IOBUF_SZ        api->iobuf_size
static char name_scratch[64][64];
static FatEnt fe_scratch[64];

enum { EM_NONE, EM_OPEN, EM_FIND, EM_GUARD };
enum { PA_NONE, PA_NEW, PA_OPEN };

typedef struct {
    char buf[FS_MAXFILE];
    int  len, cur, scroll;
    int  xscroll;
    int  cols, rows;
    int  sbdrag;
    char name[FS_NAMELEN];
    u8   mod, ro, wrap;
    char msg[40];
    int  mode, pending;
    u8   src;
    char fullpath[96];
    char fbuf[32];
    int  flen;
    int  pick_scroll;
    int  pick_drive;
} Ed;

static Ed eds[MAXINST];
static Ed *E;
static int edcols, edrows;

#define TB_H  22
#define ST_H  16
#define GUTW  40

static void edit_client_size(int *w, int *h)
{
    *w = GUTW + edcols * 8 + 8;
    *h = TB_H + edrows * 16 + ST_H;
}

static void edit_min_client(int *w, int *h)
{
    *w = GUTW + 28 * 8 + 8;
    *h = TB_H + 4 * 16 + ST_H;
}

static void edit_init(void)
{
    edcols = (SW - 96) / 8;
    if (edcols > 60) edcols = 60;
    if (edcols < 28) edcols = 28;
    edrows = (SH - 150) / 16;
    if (edrows > 18) edrows = 18;
    if (edrows < 4) edrows = 4;
}

static void ed_reset(void)
{
    E->len = E->cur = E->scroll = E->xscroll = 0;
    E->sbdrag = 0;
    E->name[0] = 0;
    E->src = 0;
    E->fullpath[0] = 0;
    E->mod = E->ro = 0;
    E->mode = EM_NONE;
    E->msg[0] = 0;
}

static void edit_new(int inst)
{
    E = &eds[inst];
    ed_reset();
    E->wrap = 0;
    E->cols = edcols;
    E->rows = edrows;
}

static void strip_cr(void)
{
    int o = 0;
    for (int i = 0; i < E->len; i++)
        if (E->buf[i] != '\r') E->buf[o++] = E->buf[i];
    E->len = o;
}

static void mark_truncated(void)
{
    E->ro = 1;
    strlcpy(E->msg, "big file: first 64 KB, read-only", sizeof E->msg);
}

static void edit_load(int inst, const char *name)
{
    E = &eds[inst];
    ed_reset();
    int n = fs_read(name, (u8 *)E->buf, sizeof E->buf);
    if (n < 0) { strlcpy(E->msg, "read error", sizeof E->msg); return; }
    E->len = n;
    strip_cr();
    strlcpy(E->name, name, FS_NAMELEN);
    for (int i = 0; i < FS_NFILES; i++) {
        FsEnt *e = fs_slot(i);
        if (e && e->used && !strcmp(e->name, name)) {
            if (e->size > sizeof E->buf) mark_truncated();
            break;
        }
    }
}

static int edit_seed(int inst, const char *name, const u8 *d, int n)
{
    E = &eds[inst];
    ed_reset();
    int trunc = 0;
    if (n > (int)sizeof E->buf) { n = sizeof E->buf; trunc = 1; }
    if (n < 0) n = 0;
    memcpy(E->buf, d, n);
    E->len = n;
    strip_cr();
    strlcpy(E->name, name, FS_NAMELEN);
    return trunc;
}

static void edit_open_a(int inst, const char *name, const u8 *d, int n)
{
    if (edit_seed(inst, name, d, n)) mark_truncated();
}

static void edit_open_usb(int inst, const char *name, const char *fullpath,
                   const u8 *d, int n)
{
    int trunc = edit_seed(inst, name, d, n);
    strlcpy(E->fullpath, fullpath, sizeof E->fullpath);
    E->src = 1;
    E->ro = fat_writable() ? 0 : 1;
    if (trunc) mark_truncated();
}

static void ed_load_usb(const char *fullpath)
{
    int n = fat_read(fullpath, iobuf, FS_MAXFILE + 512);
    const char *nm = fullpath;
    for (const char *p = fullpath; *p; p++) if (*p == '/') nm = p + 1;
    if (n < 0) { ed_reset(); strlcpy(E->msg, "usb read error", sizeof E->msg); return; }
    ed_reset();
    int trunc = 0;
    if (n > FS_MAXFILE) { n = FS_MAXFILE; trunc = 1; }
    memcpy(E->buf, iobuf, n);
    E->len = n;
    strip_cr();
    strlcpy(E->name, nm, FS_NAMELEN);
    strlcpy(E->fullpath, fullpath, sizeof E->fullpath);
    E->src = 1;
    E->ro = fat_writable() ? 0 : 1;
    if (trunc) mark_truncated();
}

static int line_start(int li)
{
    int i = 0;
    while (li > 0 && i < E->len) { if (E->buf[i] == '\n') li--; i++; }
    return i;
}
static int nlines(void)
{
    int n = 1;
    for (int i = 0; i < E->len; i++) if (E->buf[i] == '\n') n++;
    return n;
}
static int seg_count(int len)
{
    if (!E->wrap) return 1;
    int s = (len + E->cols - 1) / E->cols;
    return s ? s : 1;
}
static void cur_rc(int *row, int *col)
{
    int r = 0, c = 0;
    for (int i = 0; i < E->cur; i++) {
        if (E->buf[i] == '\n') { r++; c = 0; }
        else c++;
    }
    *row = r; *col = c;
}

typedef struct {
    int nlines, vrows, maxll;
    int cvr, cvc;
} EdSt;

static void ed_stats(EdSt *o)
{
    int ll = 0, vrows = 0, maxll = 0, lines = 0;
    int cvr = 0, cvc = 0;
    for (int i = 0; ; i++) {
        if (i == E->cur) {
            if (E->wrap) { cvr = vrows + ll / E->cols; cvc = ll % E->cols; }
            else         { cvr = vrows; cvc = ll; }
        }
        if (i >= E->len || E->buf[i] == '\n') {
            if (ll > maxll) maxll = ll;
            vrows += seg_count(ll);
            lines++;
            ll = 0;
            if (i >= E->len) break;
        } else ll++;
    }
    o->nlines = lines;
    o->vrows = vrows;
    o->maxll = maxll;
    o->cvr = cvr;
    o->cvc = cvc;
}

static int total_vrows(void)
{
    EdSt st;
    ed_stats(&st);
    return st.vrows;
}

static int max_line_len(void)
{
    EdSt st;
    ed_stats(&st);
    return st.maxll;
}

static void scroll_to_cursor(void)
{
    EdSt st;
    ed_stats(&st);
    int vr = st.cvr, vc = st.cvc;
    if (vr < E->scroll) E->scroll = vr;
    if (vr >= E->scroll + E->rows) E->scroll = vr - E->rows + 1;
    if (E->scroll < 0) E->scroll = 0;
    if (E->wrap) { E->xscroll = 0; return; }
    if (vc < E->xscroll) E->xscroll = vc;
    if (vc >= E->xscroll + E->cols) E->xscroll = vc - E->cols + 1;
    if (E->xscroll < 0) E->xscroll = 0;
}

static void ed_layout(int cw, int ch, int *textW, int *textH, int *hbar)
{
    *hbar = !E->wrap;
    *textW = cw - GUTW - SB_W;
    *textH = ch - TB_H - ST_H - (*hbar ? SB_W : 0);
    if (*textH < 16) *textH = 16;
    int cols = (*textW - 8) / 8, rows = *textH / 16;

    if (cols > 128) cols = 128; if (cols < 16) cols = 16;
    if (rows > 48) rows = 48; if (rows < 3) rows = 3;
    E->cols = cols; E->rows = rows;
}

static void ins(char ch)
{
    if (E->ro) { strlcpy(E->msg, "read-only (Save copies)", sizeof E->msg); return; }
    if (E->len >= (int)sizeof E->buf - 1) { strlcpy(E->msg, "buffer full", sizeof E->msg); return; }
    memmove(E->buf + E->cur + 1, E->buf + E->cur, E->len - E->cur);
    E->buf[E->cur++] = ch;
    E->len++;
    E->mod = 1;
    E->msg[0] = 0;
}
static void del_at(int i)
{
    if (E->ro || i < 0 || i >= E->len) return;
    memmove(E->buf + i, E->buf + i + 1, E->len - i - 1);
    E->len--;
    E->mod = 1;
    E->msg[0] = 0;
}
static void move_vert(int delta)
{
    int r, c;
    cur_rc(&r, &c);
    int nr = r + delta;
    if (nr < 0) nr = 0;
    if (nr >= nlines()) { E->cur = E->len; return; }
    int i = line_start(nr), ll = 0;
    while (i + ll < E->len && E->buf[i + ll] != '\n') ll++;
    E->cur = i + (c < ll ? c : ll);
}

static void save_write(void)
{
    int r;
    if (E->src == 1) r = fat_write(E->fullpath, (u8 *)E->buf, E->len);
    else             r = fs_write(E->name, (u8 *)E->buf, E->len);
    if (r == 0)       { E->mod = 0; strlcpy(E->msg, E->src ? "saved to USB" : "saved", sizeof E->msg); }
    else if (r == -2) strlcpy(E->msg, "disk full", sizeof E->msg);
    else if (r == -3) strlcpy(E->msg, "that name is a folder", sizeof E->msg);
    else              strlcpy(E->msg, "write error", sizeof E->msg);
}

static void save_picked(const char *path, void *ctx)
{
    E = (Ed *)ctx;
    if (!path) return;
    int drive;
    char bare[sizeof E->fullpath];
    sh_spec_split(path, &drive, bare, sizeof bare);
    if (drive == 0) {
        strlcpy(E->name, bare, FS_NAMELEN);
        E->src = 0; E->fullpath[0] = 0;
    } else {
        strlcpy(E->fullpath, bare, sizeof E->fullpath);
        strlcpy(E->name, api->path_base(bare), FS_NAMELEN);
        E->src = 1;
    }
    E->ro = 0;
    save_write();
}

static void do_save(void)
{
    if (E->name[0] && !E->ro) { save_write(); return; }
    api->file_save("Save As", "", E->name[0] ? E->name : "untitled.txt",
                   save_picked, E);
}

static void pick_usb_refresh(void);

static void run_pending(void)
{
    int p = E->pending;
    E->pending = PA_NONE;
    E->mode = EM_NONE;
    if (p == PA_NEW) ed_reset();
    else if (p == PA_OPEN) {
        E->mode = EM_OPEN; E->pick_scroll = 0;
        if (E->pick_drive == 1) pick_usb_refresh();
    }
}
static void guarded(int action)
{
    E->pending = action;
    if (E->mod) E->mode = EM_GUARD;
    else run_pending();
}

static char ed_low(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static void find_next(void)
{
    if (!E->flen) return;
    for (int off = 1; off <= E->len; off++) {
        int i = (E->cur + off) % (E->len + 1);
        if (i + E->flen > E->len) continue;
        int k = 0;

        while (k < E->flen && ed_low(E->buf[i + k]) == ed_low(E->fbuf[k])) k++;
        if (k == E->flen) {
            E->cur = i;
            E->msg[0] = 0;
            scroll_to_cursor();
            return;
        }
    }
    strlcpy(E->msg, "not found", sizeof E->msg);
}

static void edit_key(int inst, int k)
{
    E = &eds[inst];
    if (E->mode == EM_GUARD) {
        if (k == 'y' || k == 'Y') run_pending();
        else if (k == 'n' || k == 'N' || k == 27) { E->pending = PA_NONE; E->mode = EM_NONE; }
        return;
    }
    if (E->mode == EM_FIND) {
        if (k == '\n') find_next();
        else if (k == 27) E->mode = EM_NONE;
        else if (k == '\b') { if (E->flen) E->fbuf[--E->flen] = 0; }
        else if (k == 0x16) {
            char clip[64];
            if (api->clip_get_text(clip, sizeof clip) > 0)
                E->flen = cl_paste(E->fbuf, E->flen, sizeof E->fbuf, clip);
        }
        else if (k >= 32 && k < 127 && E->flen < (int)sizeof E->fbuf - 1) {
            E->fbuf[E->flen++] = (char)k; E->fbuf[E->flen] = 0;
        }
        return;
    }
    if (E->mode == EM_OPEN) {
        if (k == 27) E->mode = EM_NONE;
        return;
    }

    switch (k) {
    case 0x13: do_save(); break;
    case 0x03: {
        int a = E->cur, b = E->cur;
        while (a > 0 && E->buf[a - 1] != '\n') a--;
        while (b < E->len && E->buf[b] != '\n') b++;
        char line[256];
        int n = b - a;
        if (n > (int)sizeof line - 1) n = sizeof line - 1;
        for (int i = 0; i < n; i++) line[i] = E->buf[a + i];
        line[n] = 0;
        api->clip_set_text(line);
        strlcpy(E->msg, "line copied", sizeof E->msg);
        break;
    }
    case 0x16: {
        char clip[1024];
        if (api->clip_get_text(clip, sizeof clip) > 0) {
            for (const char *p = clip; *p; p++)
                if (*p == '\n' || ((u8)*p >= 32 && (u8)*p != 127)) ins(*p);
        }
        break;
    }
    case 0x06: E->mode = EM_FIND; E->flen = 0; E->fbuf[0] = 0; break;
    case 0x07: find_next(); break;
    case 0x0E: guarded(PA_NEW); break;
    case 0x0F: guarded(PA_OPEN); break;
    case 0x17: E->wrap = !E->wrap; E->scroll = E->xscroll = 0; break;
    case '\n': ins('\n'); break;
    case '\b': if (E->cur > 0) { E->cur--; del_at(E->cur); } break;
    case K_DEL: del_at(E->cur); break;
    case '\t': ins(' '); ins(' '); ins(' '); ins(' '); break;
    case K_LEFT:  if (E->cur > 0) E->cur--; break;
    case K_RIGHT: if (E->cur < E->len) E->cur++; break;
    case K_UP:   move_vert(-1); break;
    case K_DOWN: move_vert(1); break;
    case K_PGUP: move_vert(-E->rows); break;
    case K_PGDN: move_vert(E->rows); break;
    case K_HOME: { int r, c; cur_rc(&r, &c); E->cur = line_start(r); break; }
    case K_END:  { int i = E->cur;
                   while (i < E->len && E->buf[i] != '\n') i++;
                   E->cur = i; break; }
    default: if (k >= 32 && k <= 126) ins((char)k); break;
    }
    scroll_to_cursor();
}

#define PICK_ROWS 8
#define PICK_MAX 64

static char pick_usb[PICK_MAX][64];
static int  pick_usb_n;

static void pick_usb_refresh(void)
{
    pick_usb_n = 0;
    if (!usb_present()) return;
    int cnt = fat_list("/", fe_scratch, PICK_MAX);
    for (int i = 0; i < cnt && pick_usb_n < PICK_MAX; i++)
        if (!fe_scratch[i].is_dir)
            strlcpy(pick_usb[pick_usb_n++], fe_scratch[i].name, 64);
}

static int pick_files(char names[][64], int max, int drive)
{
    int n = 0;
    if (drive == 1) {
        for (int i = 0; i < pick_usb_n && n < max; i++)
            strlcpy(names[n++], pick_usb[i], 64);
        return n;
    }
    if (!fs_ensure()) return 0;
    for (int i = 0; i < FS_NFILES && n < max; i++) {
        FsEnt *e = fs_slot(i);
        if (e->used) strlcpy(names[n++], e->name, 64);
    }
    return n;
}
static void picker_click(int lx, int ly)
{
    char (*names)[64] = name_scratch;
    int bx = GUTW, by = TB_H + 4;
    int bw = E->cols * 8 + 8 - GUTW - 8;
    if (lx < bx || lx > bx + bw) { E->mode = EM_NONE; return; }
    if (ly >= by && ly < by + 20) {
        if (lx >= bx + bw - 88 && lx < bx + bw - 46) { E->pick_drive = 0; E->pick_scroll = 0; }
        else if (lx >= bx + bw - 44 && lx < bx + bw - 2 && usb_present()) {
            E->pick_drive = 1; E->pick_scroll = 0;
            pick_usb_refresh();
        }
        return;
    }
    int n = pick_files(names, PICK_MAX, E->pick_drive);
    int row = (ly - by - 20) / 16 + E->pick_scroll;
    if (row >= 0 && row < n) {
        char nm[80];
        Ed *keep = E;
        int inst = (int)(keep - eds);
        if (E->pick_drive == 1) {
            kfmt(nm, sizeof nm, "/%s", names[row]);
            ed_load_usb(nm);
        } else {
            strlcpy(nm, names[row], 64);
            edit_load(inst, nm);
        }
        E = keep;
    }
}

static void edit_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    E = &eds[inst];
    int textW, textH, hbar;
    ed_layout(cw, ch, &textW, &textH, &hbar);

    if (E->sbdrag && ev != EV_PRESS) {
        if (ev == EV_RELEASE) { E->sbdrag = 0; return; }
        if (E->sbdrag == 1)
            E->scroll = sbar_from_pos(textH, total_vrows(), E->rows, ly - TB_H);
        else
            E->xscroll = sbar_from_pos(textW, max_line_len() + 1, E->cols, lx - GUTW);
        return;
    }
    if (ev != EV_PRESS) return;
    if (E->mode == EM_OPEN) { picker_click(lx, ly); return; }
    if (E->mode == EM_GUARD) return;

    if (lx >= GUTW + textW && ly >= TB_H && ly < TB_H + textH) {
        E->sbdrag = 1;
        E->scroll = sbar_from_pos(textH, total_vrows(), E->rows, ly - TB_H);
        return;
    }
    if (hbar && ly >= TB_H + textH && ly < TB_H + textH + SB_W && lx >= GUTW) {
        E->sbdrag = 2;
        E->xscroll = sbar_from_pos(textW, max_line_len() + 1, E->cols, lx - GUTW);
        return;
    }

    if (ly < TB_H) {
        int b = lx / 52;
        switch (b) {
        case 0: guarded(PA_NEW); break;
        case 1: guarded(PA_OPEN); break;
        case 2: do_save(); break;
        case 3: E->mode = EM_FIND; E->flen = 0; E->fbuf[0] = 0; break;
        case 4: E->wrap = !E->wrap; E->scroll = E->xscroll = 0; scroll_to_cursor(); break;
        }
        return;
    }
    if (E->mode == EM_FIND) return;

    int ty = ly - TB_H;
    if (ty >= textH) return;
    int target = E->scroll + ty / 16;
    int col = (lx - GUTW - 4) / 8;
    if (col < 0) col = 0;
    if (!E->wrap) col += E->xscroll;

    int v = 0, i = 0;
    for (;;) {
        int ll = 0;
        while (i + ll < E->len && E->buf[i + ll] != '\n') ll++;
        int segs = seg_count(ll);
        if (target < v + segs) {
            int seg = target - v;
            int base = E->wrap ? seg * E->cols : 0;
            int c = base + col;
            if (c > ll) c = ll;
            E->cur = i + c;
            return;
        }
        v += segs;
        if (i + ll >= E->len) break;
        i += ll + 1;
    }
    E->cur = E->len;
}

static void draw_picker(int cx, int cy)
{
    char (*names)[64] = name_scratch;
    int n = pick_files(names, PICK_MAX, E->pick_drive);
    int bx = cx + GUTW, by = cy + TB_H + 4;
    int bw = E->cols * 8 + 8 - GUTW - 8;
    int bh = (PICK_ROWS + 1) * 16 + 8;
    panel(bx, by, bw, bh, 0);
    fill_rect(bx + 2, by + 2, bw - 4, 18, C_NAVY);
    draw_text(bx + 6, by + 3, "Open  (Esc cancels)", C_WHITE);
    panel(bx + bw - 88, by + 2, 40, 16, E->pick_drive == 0);
    draw_text(bx + bw - 82, by + 3, "A:", C_BLACK);
    panel(bx + bw - 44, by + 2, 40, 16, E->pick_drive == 1);
    draw_text(bx + bw - 40, by + 3, "USB", usb_present() ? C_BLACK : C_G0 + 4);
    fill_rect(bx + 2, by + 20, bw - 4, PICK_ROWS * 16, C_WHITE);
    if (n == 0) draw_text(bx + 8, by + 24, E->pick_drive ? "(no files on USB)" : "(no files on A:)", C_G0 + 3);
    for (int v = 0; v < PICK_ROWS; v++) {
        int idx = E->pick_scroll + v;
        if (idx >= n) break;
        int rowy = by + 22 + v * 16;
        int hov = mx >= bx + 2 && mx < bx + bw - 2 && my >= rowy && my < rowy + 16;
        if (hov) fill_rect(bx + 2, rowy - 2, bw - 4, 16, C_HILITE);
        draw_text_clip(bx + 8, rowy, names[idx], hov ? C_WHITE : C_BLACK, bw - 16);
    }
}

static void edit_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    E = &eds[w->inst];
    int textW, textH, hbar;
    ed_layout(cw, ch, &textW, &textH, &hbar);

    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, TB_H, GRGB(214, 218, 228),
                           GRGB(180, 186, 202), 1);
        gfx->set_dither(0);
    } else fill_rect(cx, cy, cw, TB_H, C_FACE);
    hline(cx, cy + TB_H - 1, cw, C_SHAD);
    static const char *const tb[5] = { "New", "Open", "Save", "Find", "Wrap" };
    for (int b = 0; b < 5; b++) {
        UiRect r = ui_r(b * 52 + 2, 2, 48, 18);
        int hov = mx >= cx + r.x && mx < cx + r.x + r.w &&
                  my >= cy + r.y && my < cy + r.y + r.h;
        int on  = (b == 4 && E->wrap) || hov;
        int en  = !(b == 2 && (E->ro || !E->mod));
        ui_button(cx, cy, r, tb[b], on, en);
    }

    if (E->name[0]) {
        char nm[40];
        kfmt(nm, sizeof nm, "%s%s", E->name, E->mod ? " *" : "");
        int nw = (int)strlen(nm) * 8;
        if (5 * 52 + 8 + nw < cw - 8)
            draw_text(cx + cw - 8 - nw, cy + 6, nm, C_NAVY);
    }

    int tx = cx + GUTW, ty = cy + TB_H;
    fill_rect(cx, ty, GUTW, textH, C_G0 + 1);
    vline(tx - 1, ty, textH, C_SHAD);
    fill_rect(tx, ty, textW, textH, E->ro ? C_G0 + 7 : C_WHITE);

    EdSt es;
    ed_stats(&es);
    int cvr = es.cvr, cvc = es.cvc;

    if (E->scroll > es.vrows - E->rows) E->scroll = es.vrows - E->rows;
    if (E->scroll < 0) E->scroll = 0;

    int v = 0, ls = 0, li = 0;
    while (ls <= E->len) {
        int ll = 0;
        while (ls + ll < E->len && E->buf[ls + ll] != '\n') ll++;
        int segs = seg_count(ll);
        for (int s = 0; s < segs; s++, v++) {
            if (v < E->scroll) continue;
            int vy = v - E->scroll;
            if (vy >= E->rows) goto done;
            int yy = ty + vy * 16;
            if (s == 0) {
                char ln[8];
                kfmt(ln, sizeof ln, "%4d", li + 1);
                draw_text(cx + 4, yy, ln, C_G0 + 5);
            }
            int from = E->wrap ? s * E->cols : E->xscroll;
            int cnt = ll - from;
            if (cnt > E->cols) cnt = E->cols;
            for (int c = 0; c < cnt; c++)
                draw_char(tx + 4 + c * 8, yy, E->buf[ls + from + c], C_BLACK);
            if (!E->wrap && ll - E->xscroll > E->cols)
                draw_char(tx + 4 + (E->cols - 1) * 8, yy, '\x1a', C_G0 + 4);
        }
        ls += ll + 1;
        li++;
    }
done:
    if (win_is_focused(w) && gui_blink && E->mode == EM_NONE &&
        cvr >= E->scroll && cvr < E->scroll + E->rows) {
        int vcx = E->wrap ? cvc : cvc - E->xscroll;
        if (vcx >= 0 && vcx <= E->cols)
            fill_rect(tx + 4 + vcx * 8, ty + (cvr - E->scroll) * 16, 2, 16, C_BLACK);
    }

    draw_sbar(cx + GUTW + textW, ty, textH, 0, es.vrows, E->rows, E->scroll);
    if (hbar) {
        draw_sbar(tx, ty + textH, textW, 1, es.maxll + 1, E->cols, E->xscroll);
        fill_rect(cx, ty + textH, GUTW, SB_W, C_FACE);
    }
    fill_rect(cx + GUTW + textW, ty + textH, SB_W, hbar ? SB_W : 0, C_FACE);

    int sy = cy + TB_H + textH + (hbar ? SB_W : 0);
    fill_rect(cx, sy, cw, ST_H, C_FACE);
    hline(cx, sy, cw, C_SHAD);
    hline(cx, sy + 1, cw, C_LIGHT);
    int r, c;
    cur_rc(&r, &c);
    char st[72];
    const char *tag = E->ro ? "[read-only] " : (E->mod ? "modified " : "");
    const char *loc = E->name[0] ? (E->src ? "USB " : "A: ") : "";
    if (E->mode == EM_FIND) kfmt(st, sizeof st, "Find: %s_  (Enter=next, Esc)", E->fbuf);
    else kfmt(st, sizeof st, "%sLn %d Col %d  %d B %s%s", loc, r + 1, c + 1, E->len, tag,
              E->msg[0] ? E->msg : (E->wrap ? "wrap" : ""));
    draw_text(cx + 4, sy + 1, st, C_G0 + 1);

    if (E->mode == EM_OPEN) draw_picker(cx, cy);
    if (E->mode == EM_GUARD) {
        int bw = 260, bh = 60, bx = cx + (cw - bw) / 2, by = cy + TB_H + 20;
        panel(bx, by, bw, bh, 0);
        draw_text(bx + 12, by + 12, "Discard unsaved changes?", C_BLACK);
        draw_text(bx + 12, by + 34, "Y = discard    N = cancel", C_NAVY);
    }
}

static void edit_csize(int inst, int *w, int *h) { (void)inst; edit_client_size(w, h); }

static int edit_opener(const char *name, const char *fullpath,
                       const u8 *data, int n)
{
    int inst = win_open(edit_type);
    if (inst < 0) return -1;
    if (fullpath)
        edit_open_usb(inst, name, fullpath, data, n);
    else
        edit_open_a(inst, name, data, n);
    return 0;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Editor"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    ui_init(k, gfx);
    edit_init();
    static const AppDesc d = {
        .title = "Editor", .max_inst = MAXINST, .resizable = 1, .in_menu = 1,
        .open = edit_new, .draw = edit_draw, .key = edit_key,
        .mouse = edit_mouse, .client_size = edit_csize,
        .min_client = edit_min_client,
    };
    edit_type = api->register_app(&d);
    if (edit_type < 0) return 1;
    api->register_opener("", edit_opener);
    return 0;
}
