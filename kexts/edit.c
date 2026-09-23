#include "kapi.h"
#include "shpath.h"
#include "clipline.inc"
#include "buffer_core.inc"
#include "gdi.h"
#include "menushade.h"

#include "ui.inc"

static const Kapi *api;
#include "apptext.h"
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
enum { PA_NONE, PA_NEW, PA_OPEN, PA_CLOSE, PA_FILE };

typedef struct {
    char *buf;
    u32 capacity;
    int  len, cur, scroll;
    int anchor, selecting;
    int  xscroll;
    int  cols, rows;
    int  sbdrag;
    char name[FS_NAMELEN];
    u8   mod, ro, wrap;
    char msg[40];
    int  mode, pending;
    int  menu, menu_row;
    u8   src;
    char fullpath[96];
    char open_path[96];
    int open_src;
    char fbuf[32];
    int  flen;
    int  pick_scroll;
    int  pick_drive;
} Ed;

static Ed eds[MAXINST];
static Ed *E;
static int edcols, edrows;

#define MENU_H 24
#define MENU_W 232
#define TB_H  50
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
    *h = TB_H + 6 * 16 + ST_H;
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
    E->anchor = E->selecting = 0;
    E->sbdrag = 0;
    E->name[0] = 0;
    E->src = 0;
    E->fullpath[0] = 0;
    E->mod = E->ro = 0;
    E->mode = EM_NONE;
    E->pending = PA_NONE;
    E->menu = E->menu_row = -1;
    E->msg[0] = 0;
    if (E->buf) E->buf[0] = 0;
}

static int edit_buffer(u32 needed)
{
    if (needed > FS_MAXFILE) return 0;
    if (E->buf && needed <= E->capacity) return 1;
    u32 cap = buffer_capacity(E->capacity, needed, 1024, FS_MAXFILE);
    char *next = api->krealloc(E->buf, cap + 1);
    if (!next && cap > needed && E->buf) {
        cap = needed;
        next = api->krealloc(E->buf, cap + 1);
    }
    if (!next) { strlcpy(E->msg, "Not enough memory; text kept", sizeof E->msg); return 0; }
    if (!E->buf) next[0] = 0;
    E->buf = next;
    E->capacity = cap;
    if (api->mem_track) api->mem_track("Editor text", E->buf, cap + 1);
    return 1;
}

static void edit_trim(void)
{
    u32 cap = buffer_capacity(0, (u32)E->len, 1024, FS_MAXFILE);
    if (!E->buf || cap >= E->capacity) return;
    char *next = api->krealloc(E->buf, cap + 1);
    if (!next) return;
    E->buf = next;
    E->capacity = cap;
    if (api->mem_track) api->mem_track("Editor text", E->buf, cap + 1);
}

static void edit_new(int inst)
{
    E = &eds[inst];
    ed_reset();
    edit_trim();
    E->wrap = 0;
    E->cols = edcols;
    E->rows = edrows;
    edit_buffer(0);
}

static void edit_close(int inst)
{
    E = &eds[inst];
    if (E->buf) api->kfree(E->buf);
    E->buf = 0;
    E->capacity = 0;
    ed_reset();
}

static void strip_cr(void)
{
    int o = 0;
    for (int i = 0; i < E->len; i++)
        if (E->buf[i] != '\r') E->buf[o++] = E->buf[i];
    E->len = o;
    E->buf[o] = 0;
}

static void mark_truncated(void)
{
    E->ro = 1;
    strlcpy(E->msg, "First 512 KiB only; read-only", sizeof E->msg);
}

static int edit_seed(int inst, const char *name, const u8 *d, int n)
{
    E = &eds[inst];
    int trunc = 0;
    if (n > FS_MAXFILE) { n = FS_MAXFILE; trunc = 1; }
    if (n < 0) n = 0;
    if (!edit_buffer((u32)n)) return -1;
    ed_reset();
    memcpy(E->buf, d, n);
    E->len = n;
    strip_cr();
    edit_trim();
    strlcpy(E->name, name, FS_NAMELEN);
    return trunc;
}

static void edit_open_a(int inst, const char *name, const u8 *d, int n)
{
    if (edit_seed(inst, name, d, n) > 0) mark_truncated();
}

static void edit_open_usb(int inst, const char *name, const char *fullpath,
                   const u8 *d, int n)
{
    int trunc = edit_seed(inst, name, d, n);
    if (trunc < 0) return;
    strlcpy(E->fullpath, fullpath, sizeof E->fullpath);
    E->src = 1;
    E->ro = fat_writable() ? 0 : 1;
    if (trunc) mark_truncated();
}

static void edit_load(int inst, const char *name)
{
    api->buffer_lock();
    int n = fs_read(name, iobuf, FS_MAXFILE + 1);
    E = &eds[inst];
    if (n < 0) strlcpy(E->msg, "read error; text kept", sizeof E->msg);
    else edit_open_a(inst, name, iobuf, n);
    api->buffer_unlock();
}

static void ed_load_usb(const char *fullpath)
{
    int inst = (int)(E - eds);
    api->buffer_lock();
    int n = fat_read(fullpath, iobuf, FS_MAXFILE + 1);
    E = &eds[inst];
    const char *nm = fullpath;
    for (const char *p = fullpath; *p; p++) if (*p == '/') nm = p + 1;
    if (n < 0) strlcpy(E->msg, "usb read error; text kept", sizeof E->msg);
    else edit_open_usb(inst, nm, fullpath, iobuf, n);
    api->buffer_unlock();
}

static int seg_count(int len)
{
    if (!E->wrap) return 1;
    int s = len / E->cols + 1;
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

static int edit_grow(TextEdit *t, int need)
{
    if (!edit_buffer((u32)need)) return 0;
    t->buf = E->buf;
    t->cap = (int)E->capacity + 1;
    return 1;
}
static void move_vert(int delta)
{
    EdSt st;
    ed_stats(&st);
    int target = st.cvr + delta, v = 0, i = 0;
    if (target < 0) target = 0;
    for (;;) {
        int ll = 0;
        while (i + ll < E->len && E->buf[i + ll] != '\n') ll++;
        int segs = seg_count(ll);
        if (target < v + segs) {
            int c = (E->wrap ? (target - v) * E->cols : 0) + st.cvc;
            if (E->wrap && st.cvc >= E->cols) c = (target - v + 1) * E->cols - 1;
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

static void run_pending(void);
static void save_write(void)
{
    if (!E->buf) { strlcpy(E->msg, "Not enough memory to save", sizeof E->msg); return; }
    Ed *keep = E;
    int r;
    if (E->src == 1) r = fat_write(E->fullpath, (u8 *)E->buf, E->len);
    else             r = fs_write(E->name, (u8 *)E->buf, E->len);
    E = keep;
    if (r == 0)       { E->mod = 0; strlcpy(E->msg, E->src ? "saved to USB" : "saved", sizeof E->msg); }
    else if (r == -2) strlcpy(E->msg, "disk full", sizeof E->msg);
    else if (r == -3) strlcpy(E->msg, "that name is a folder", sizeof E->msg);
    else              strlcpy(E->msg, "write error", sizeof E->msg);
    if(r==0)run_pending();else E->pending=PA_NONE;
}

static void save_picked(const char *path, void *ctx)
{
    E = (Ed *)ctx;
    if (!path) { E->pending=PA_NONE;return; }
    int drive;
    char bare[sizeof E->fullpath];
    sh_spec_split(path, &drive, bare, sizeof bare);
    if (E->ro && drive == E->src &&
        (drive ? !strcasecmp(bare, E->fullpath) : !strcmp(bare, E->name))) {
        strlcpy(E->msg, "Choose a different file name", sizeof E->msg);
        E->pending=PA_NONE;
        return;
    }
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
    if (!E->buf) { strlcpy(E->msg, "Not enough memory to save", sizeof E->msg); return; }
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
    if (p == PA_NEW) { ed_reset(); edit_trim(); }
    else if (p == PA_OPEN) {
        E->mode = EM_OPEN; E->pick_scroll = 0;
        if (E->pick_drive == 1) pick_usb_refresh();
    }
    else if(p==PA_CLOSE)api->win_close_self(edit_type,(int)(E-eds));
    else if(p==PA_FILE){if(E->open_src)ed_load_usb(E->open_path);else edit_load((int)(E-eds),E->open_path);}
}
static void guard_answer(int result,void *ctx)
{
    E=(Ed *)ctx;E->mode=EM_NONE;
    if(result==MBR_YES)do_save();
    else if(result==MBR_NO)run_pending();
    else E->pending=PA_NONE;
}
static void guarded(int action)
{
    if(E->pending)return;
    E->pending = action;
    if (E->mod) { E->mode=EM_GUARD;api->msgbox("Unsaved changes","Save changes to this document?",MB_SAVEDISCARD,guard_answer,E); }
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
            E->anchor = i;
            E->cur = i + E->flen;
            E->msg[0] = 0;
            scroll_to_cursor();
            return;
        }
    }
    strlcpy(E->msg, "not found", sizeof E->msg);
}

enum { EC_SAVE_AS = 0x200 };

typedef struct {
    const char *label, *shortcut;
    int key;
} EdMenuItem;

static const EdMenuItem ed_menus[3][6] = {
    { { "New", "Ctrl+N", 14 }, { "Open...", "Ctrl+O", 15 },
      { "Save", "Ctrl+S", 19 }, { "Save As...", "", EC_SAVE_AS } },
    { { "Cut", "Ctrl+X", 24 }, { "Copy", "Ctrl+C", 3 },
      { "Paste", "Ctrl+V", 22 }, { "Select All", "Ctrl+A", 1 },
      { "Find...", "Ctrl+F", 6 }, { "Find Next", "Ctrl+G", 7 } },
    { { "Word Wrap", "Ctrl+W", 23 } }
};
static const int ed_menu_count[3] = { 4, 6, 1 };
static const int ed_toolbar_keys[5] = { 14, 15, 19, 6, 23 };

static int ed_enabled(int key)
{
    switch (key) {
    case 19: return E->buf && !E->ro && E->mod;
    case EC_SAVE_AS: return E->buf != 0;
    case 24: return !E->ro && E->cur != E->anchor;
    case 3: return E->cur != E->anchor;
    case 22: {
        const char *type = api->clip_type();
        return !E->ro && type && !strcmp(type, "text");
    }
    case 1: return E->len > 0;
    case 7: return E->flen > 0 && E->len > 0;
    default: return 1;
    }
}

static UiRect ed_menu_rect(int menu, int cw)
{
    int x = 4 + menu * 48;
    if (x + MENU_W > cw) x = cw - MENU_W;
    if (x < 0) x = 0;
    return ui_r(x, MENU_H, MENU_W, ed_menu_count[menu] * 20 + 4);
}

static UiRect ed_toolbar_rect(int button)
{
    return ui_r(4 + button * 52, MENU_H + 2, 48, 22);
}

static void edit_key(int inst, int k)
{
    E = &eds[inst];
    if(k==K_CLOSE_REQUEST){guarded(PA_CLOSE);return;}
    if (E->menu >= 0) {
        if (k == 27) { E->menu = E->menu_row = -1; return; }
        if (k == K_LEFT || k == K_RIGHT) {
            E->menu = (E->menu + (k == K_LEFT ? 2 : 1)) % 3;
            E->menu_row = -1;
            return;
        }
        if (k == K_UP || k == K_DOWN) {
            int n = ed_menu_count[E->menu];
            int row = E->menu_row;
            if (row < 0) row = k == K_DOWN ? n - 1 : 0;
            for (int i = 0; i < n; i++) {
                row = (row + (k == K_DOWN ? 1 : n - 1)) % n;
                if (ed_enabled(ed_menus[E->menu][row].key)) break;
            }
            E->menu_row = row;
            return;
        }
        if (k == '\n' || k == '\r') {
            if (E->menu_row < 0) return;
            k = ed_menus[E->menu][E->menu_row].key;
            if (!ed_enabled(k)) return;
        }
        E->menu = E->menu_row = -1;
    }
    if (E->mode == EM_GUARD) {
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

    TextEdit t = { E->buf, (int)E->capacity + 1, E->len, E->cur, E->anchor };
    int r = 0;
    switch (k) {
    case 0x13: do_save(); break;
    case EC_SAVE_AS:
        api->file_save("Save As", "", E->name[0] ? E->name : "untitled.txt", save_picked, E);
        break;
    case 0x06: E->mode = EM_FIND; E->flen = 0; E->fbuf[0] = 0; break;
    case 0x07: find_next(); break;
    case 0x0E: guarded(PA_NEW); break;
    case 0x0F: guarded(PA_OPEN); break;
    case 0x17: E->wrap = !E->wrap; E->scroll = E->xscroll = 0; break;
    case K_UP: case K_DOWN: case K_PGUP: case K_PGDN:
        move_vert(k == K_UP ? -1 : k == K_DOWN ? 1 : k == K_PGUP ? -E->rows : E->rows);
        if (!(api->kbd_mods() & 1)) E->anchor = E->cur;
        break;
    default:
        r = at_key(&t, k, 1, E->ro, edit_grow);
        E->len = t.len; E->cur = t.caret; E->anchor = t.anchor;
        if (r == 2) {
            E->mod = 1; E->msg[0] = 0;
            if (E->capacity > 1024 && (u32)E->len < E->capacity / 4) edit_trim();
        } else if (r < 0) strlcpy(E->msg, k == 3 || k == 24 ? "Selection exceeds clipboard capacity" : "Text did not fit; original kept", sizeof E->msg);
        break;
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
    if (ev == EV_RELEASE) { E->selecting = 0; return; }
    if (ev == EV_DRAG && E->selecting) goto select_text;
    if (ev != EV_PRESS) return;
    E->selecting = 0;
    if (E->mode == EM_OPEN) { picker_click(lx, ly); return; }
    if (E->mode == EM_GUARD) return;

    if (ly >= 2 && ly < MENU_H - 2 && lx >= 4 && lx < 4 + 3 * 48) {
        int menu = (lx - 4) / 48;
        E->menu = E->menu == menu ? -1 : menu;
        E->menu_row = -1;
        E->mode = EM_NONE;
        return;
    }
    if (E->menu >= 0) {
        UiRect r = ed_menu_rect(E->menu, cw);
        if (ui_hit(r, lx, ly)) {
            int row = (ly - r.y - 2) / 20;
            if (ly >= r.y + 2 && ly < r.y + r.h - 2 &&
                lx >= r.x + 2 && lx < r.x + r.w - 2 &&
                row < ed_menu_count[E->menu]) {
                int key = ed_menus[E->menu][row].key;
                if (ed_enabled(key)) {
                    E->menu = E->menu_row = -1;
                    edit_key(inst, key);
                }
            }
        } else E->menu = E->menu_row = -1;
        return;
    }

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
        for (int b = 0; b < 5; b++) {
            if (ui_hit(ed_toolbar_rect(b), lx, ly) && ed_enabled(ed_toolbar_keys[b])) {
                E->mode = EM_NONE;
                edit_key(inst, ed_toolbar_keys[b]);
                break;
            }
        }
        return;
    }
    if (E->mode == EM_FIND) return;

    if (ly >= TB_H + textH || lx < GUTW) return;
    E->selecting = 1;
select_text:
    if (ev == EV_DRAG) {
        if (ly < TB_H && E->scroll > 0) E->scroll--;
        if (ly >= TB_H + textH && E->scroll + E->rows < total_vrows()) E->scroll++;
        if (!E->wrap && lx < GUTW && E->xscroll > 0) E->xscroll--;
        if (!E->wrap && lx >= GUTW + textW && E->xscroll < max_line_len()) E->xscroll++;
    }
    int ty = ly - TB_H;
    if (ty < 0) ty = 0;
    if (ty >= textH) ty = textH - 1;
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
            if (ev == EV_PRESS && !(api->kbd_mods() & 1)) E->anchor = E->cur;
            return;
        }
        v += segs;
        if (i + ll >= E->len) break;
        i += ll + 1;
    }
    E->cur = E->len;
    if (ev == EV_PRESS && !(api->kbd_mods() & 1)) E->anchor = E->cur;
}

static void edit_wheel(int inst, int dz)
{
    E = &eds[inst];
    E->scroll -= dz * 3;
    if (E->scroll < 0) E->scroll = 0;
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
    Ed *previous=E;
    gfx = gdi_bind(api, 11);
    E = &eds[w->inst];
    int textW, textH, hbar;
    ed_layout(cw, ch, &textW, &textH, &hbar);

    menu_shade(api, cx, cy, cw, MENU_H - 1, 0);
    hline(cx, cy + MENU_H - 1, cw, C_SHAD);
    fill_rect(cx, cy + MENU_H, cw, TB_H - MENU_H, C_FACE);
    hline(cx, cy + MENU_H, cw, C_LIGHT);
    hline(cx, cy + TB_H - 1, cw, C_SHAD);
    static const char *const labels[3] = { "File", "Edit", "View" };
    int active = win_is_focused(w) && E->mode != EM_OPEN && E->mode != EM_GUARD;
    for (int m = 0; m < 3; m++) {
        int x = cx + 4 + m * 48;
        int on = active && (E->menu == m || api->control_state(x, cy + 2, 48, 20));
        if (on) menu_shade(api, x, cy + 2, 48, 20, 1);
        draw_text(x + 8, cy + 4, labels[m], on ? C_WHITE : C_BLACK);
    }
    static const char *const tb[5] = { "New", "Open", "Save", "Find", "Wrap" };
    for (int b = 0; b < 5; b++) {
        UiRect r = ed_toolbar_rect(b);
        int on  = b == 4 && E->wrap;
        int en  = E->menu < 0 && E->mode != EM_OPEN && E->mode != EM_GUARD && ed_enabled(ed_toolbar_keys[b]);
        ui_button(cx, cy, r, tb[b], on, en);
    }

    {
        char nm[40];
        kfmt(nm, sizeof nm, "%s%s", E->name[0] ? E->name : "Untitled", E->mod ? " *" : "");
        draw_text_clip(cx + 160, cy + 4, nm, C_DARK, cw - 168);
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
            int a = E->cur < E->anchor ? E->cur : E->anchor;
            int b = E->cur > E->anchor ? E->cur : E->anchor;
            for (int c = 0; c < cnt; c++) {
                int pos = ls + from + c, selected = pos >= a && pos < b;
                if (selected) fill_rect(tx + 4 + c * 8, yy, 8, 16, C_NAVY);
                draw_char(tx + 4 + c * 8, yy, E->buf[pos], selected ? C_WHITE : C_BLACK);
            }
            if (cnt >= 0 && cnt < E->cols && ls + ll < E->len && ls + ll >= a && ls + ll < b && s == segs - 1)
                fill_rect(tx + 4 + cnt * 8, yy, 8, 16, C_NAVY);
            if (!E->wrap && ll - E->xscroll > E->cols)
                draw_char(tx + 4 + (E->cols - 1) * 8, yy, '\x1a', C_G0 + 4);
        }
        ls += ll + 1;
        li++;
    }
done:
    if (win_is_focused(w) && gui_blink && E->cur == E->anchor && E->mode == EM_NONE && E->menu < 0 &&
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
    if (E->menu >= 0 && active) {
        UiRect r = ed_menu_rect(E->menu, cw);
        int x = cx + r.x, y = cy + r.y;
        panel(x, y, r.w, r.h, 0);
        int hover = -1;
        if (mx >= x + 2 && mx < x + r.w - 2 && my >= y + 2 && my < y + r.h - 2)
            hover = (my - y - 2) / 20;
        for (int i = 0; i < ed_menu_count[E->menu]; i++) {
            const EdMenuItem *item = &ed_menus[E->menu][i];
            int yy = y + 2 + i * 20;
            int enabled = ed_enabled(item->key);
            int on = enabled && i == (hover >= 0 ? hover : E->menu_row);
            u8 color = !enabled ? C_SHAD : on ? C_WHITE : C_BLACK;
            menu_shade(api, x + 2, yy, r.w - 4, 20, on);
            if (item->key == 23 && E->wrap) draw_char(x + 6, yy + 2, '*', color);
            draw_text(x + 22, yy + 2, item->label, color);
            draw_text(x + r.w - 10 - (int)strlen(item->shortcut) * 8, yy + 2, item->shortcut, color);
        }
    }
    E=previous;
}

static void edit_csize(int inst, int *w, int *h) { (void)inst; edit_client_size(w, h); }

static int edit_opener(const char *name, const char *fullpath,
                       const u8 *data, int n)
{
    int inst = win_open(edit_type);
    if (inst < 0) return -1;
    E=&eds[inst];
    if(E->pending){api->notify("Finish the current Editor dialog first.");return 0;}
    if(E->mod){
        E->open_src=fullpath!=0;strlcpy(E->open_path,fullpath?fullpath:name,sizeof E->open_path);
        guarded(PA_FILE);return 0;
    }
    if (fullpath)
        edit_open_usb(inst, name, fullpath, data, n);
    else
        edit_open_a(inst, name, data, n);
    return 0;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, KEXT_RECLAIMABLE, "Editor"
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
        .mouse = edit_mouse, .wheel = edit_wheel, .client_size = edit_csize, .close = edit_close,
        .live_draw = APP_CLOSE_REQUEST,
        .min_client = edit_min_client,
    };
    edit_type = api->register_app(&d);
    if (edit_type < 0) return 1;
    api->register_opener("", edit_opener);
    return 0;
}
