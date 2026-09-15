/* Browses floppy and USB folders and manages their files. */
#include "kapi.h"
#include "fspath.inc"
#include "archiveicon.inc"
#include "foldercopy.inc"
#include "shpath.h"
#include "fileops.inc"
#include "gdi.h"
#include "delprompt.inc"
#include "listkeep.inc"
#include "textfield.inc"
#include "menushade.h"
#include "button.h"

static const Kapi *api;
#include "fileprops.inc"
static const GdiOps *gfx;
static int files_type = -1;

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
#define rtc_now_dos     api->rtc_now_dos
#define dos_fmt         api->dos_fmt
#define SW              (*api->screen_w)
#define SH              (*api->screen_h)
#define mx              (*api->mouse_x)
#define my              (*api->mouse_y)
#define fill_rect       api->fill_rect
#define hline           api->hline
#define vline           api->vline
#define bevel           api->bevel
#define panel           api->panel
#define draw_char       api->draw_char
#define draw_text       api->draw_text
#define draw_text_clip  api->draw_text_clip
#define draw_text_clip2 api->draw_text_clip2
#define draw_text_scaled api->draw_text_scaled
#define draw_sbar       api->draw_sbar
#define sbar_from_pos   api->sbar_from_pos
#define focus_rect      api->focus_rect
#define line            api->line
#define rect            api->rect
#define circle          api->circle
#define fill_circle     api->fill_circle
#define set_clip        api->set_clip
#define clear_clip      api->clear_clip
#define palette_set     api->palette_set
#define win_open        api->win_open
#define win_close_self  api->win_close_self
#define fs_ensure()     (fs_slot(0) != 0)
#define fs_slot         api->fs_slot
#define fs_read         api->fs_read
#define fs_write        api->fs_write
#define fs_delete       api->fs_delete
#define fs_exists       api->fs_exists
#define fs_free_kb      api->fs_free_kb
#define ext_type        api->ext_type
#define fat_list        api->fat_list
#define fat_read        api->fat_read
#define fat_write       api->fat_write
#define fat_delete      api->fat_delete
#define fat_writable    api->fat_writable
#define fat_label       api->fat_label
#define fat_free_kb     api->fat_free_kb
#define usb_present     api->usb_present
#define usb_capacity_kb api->usb_capacity_kb
#define iobuf           api->iobuf
#define IOBUF_SZ        api->iobuf_size
#define opener_dispatch api->open_with
static FatEnt fe_scratch[128];

typedef struct {
    char name[64];
    u32  size, mtime;
    u8   is_dir;
    int  fslot;
} Row;

#define MAXROWS 128

#define MAXSEL  MAXROWS
#define HISTMAX 24

typedef struct {
    u8   drive;
    char a_dir[FS_NAMELEN];
    char path[128];
} Loc;

typedef struct {
    Row  rows[MAXROWS];
    int  nrows;
    int  cliW, cliH;
    int  cur_drive;
    char cur_path[128];
    char a_dir[FS_NAMELEN];
    char sel[MAXSEL][64];
    int  nsel;
    u8   click_collapse;
    char collapse_name[64];
    int  sort_col, sort_desc;
    int  colw[4];
    int  coldrag;
    int  need_refresh;
    u32  refresh_t;
    int  scroll;
    int  xs;
    int  sbdrag;
    int  btnfocus;
    u32  last_click_t;
    char last_click_name[64];
    int  renaming;
    char rnbuf[FS_NAMELEN];
    int  rnlen;
    int  rncar;
    int  rnall;
    char fm_msg[64];
    int  press_lx, press_ly;
    u8   press_row, drag_sent;
    u32  seen_gen;
    u8   band;
    int  band_x0, band_cx;
    int  band_y0, band_cur;
    Loc  hist[HISTMAX];
    int  hist_n, hist_i;
    int menu, menu_sel, menu_n;
    const char *menu_items[12];
    u8 menu_actions[12];
} Fm;

static Fm fms[MAXINST];
static Fm *F;

static int  g_cut;

#define SPECLIST 4096
static char g_specs[SPECLIST];
static char g_cut_list[SPECLIST];

static FtBatch transfers;

static int over;

static int dcliW, dcliH;

#define MENU_H   20
#define TB_H     32
#define ADDR_H   32
#define ADDR_X   80
#define TOP_H    (MENU_H + TB_H + ADDR_H)
#define HDR_H    17
#define ROW_H    16
#define SIDEBAR  150
#define DRV_H    30
#define FOOT_H   20
#define COL_SIZE 64
#define COL_TYPE 76
#define COL_MOD  116

#define GRAD0   248
#define NGRAD   8

enum { IC_BACK, IC_FWD, IC_UP, IC_CUT, IC_COPY, IC_PASTE,
       IC_FOLDER, IC_FILE, IC_DRIVE };

static const char s_left[2]  = { (char)0x1b, 0 };
static const char s_right[2] = { (char)0x1a, 0 };
static const char s_up[2]    = { (char)0x18, 0 };
static const char s_down[2]  = { (char)0x19, 0 };

enum { MA_OPEN, MA_CUT, MA_COPY, MA_PASTE, MA_RENAME, MA_DUP, MA_DELETE,
       MA_PROPS, MA_INSTALL, MA_NEW, MA_REFRESH, MA_CLOSE, MA_BACK, MA_FWD,
       MA_UP, MA_DRIVEA, MA_DRIVEU, MA_SORTN, MA_SORTS, MA_SORTT, MA_SORTD,
       MA_SELALL, MA_ABOUT, MA_NEWDIR, MA_PATH };
static u8 menu_codes[12];

static const struct { int action; int icon; const char *label; } TB[6] = {
    { MA_BACK,  IC_BACK,  "Back"    },
    { MA_FWD,   IC_FWD,   "Forward" },
    { MA_UP,    IC_UP,    "Up"      },
    { MA_CUT,   IC_CUT,   "Cut"     },
    { MA_COPY,  IC_COPY,  "Copy"    },
    { MA_PASTE, IC_PASTE, "Paste"   },
};
#define NTB 6

static const char *const MENUS[4] = { "File", "Edit", "View", "Go" };
#define NMENU  4
#define MPAD   8

static void fileman_client_size(int *w, int *h) { *w = dcliW; *h = dcliH; }

static void fileman_min_client(int *w, int *h)
{
    *w = SIDEBAR + 180 + SB_W;
    *h = TOP_H + HDR_H + 5 * ROW_H + FOOT_H;
}

static void fileman_init(void)
{
    dcliW = SW - 40;
    if (dcliW > 588) dcliW = 588;
    if (dcliW < 360) dcliW = 360;
    dcliH = SH - 140;
    if (dcliH > 320) dcliH = 320;
    if (dcliH < 170) dcliH = 170;
}

static int sel_has(const char *name)
{
    for (int i = 0; i < F->nsel; i++)
        if (!strcmp(F->sel[i], name)) return 1;
    return 0;
}
static void sel_clear(void) { F->nsel = 0; }
static void sel_add(const char *name)
{
    if (sel_has(name) || F->nsel >= MAXSEL) return;
    strlcpy(F->sel[F->nsel++], name, 64);
}
static void sel_set_single(const char *name) { F->nsel = 0; sel_add(name); }
static void sel_toggle(const char *name)
{
    for (int i = 0; i < F->nsel; i++)
        if (!strcmp(F->sel[i], name)) {
            for (; i < F->nsel - 1; i++) strlcpy(F->sel[i], F->sel[i + 1], 64);
            F->nsel--;
            return;
        }
    sel_add(name);
}
static const char *sel_focus(void) { return F->nsel ? F->sel[F->nsel - 1] : ""; }
static int row_is_sel(int idx)
{
    return idx >= 0 && idx < F->nrows && sel_has(F->rows[idx].name);
}

typedef struct {
    int lx0, lw;
    int scrolled;
    int ncw;
    int size_x, type_x, mod_x;
    int list_h, vis;
} FmGeo;

static void fm_geo(int cw, int ch, FmGeo *g)
{
    g->lx0 = SIDEBAR + 2;
    g->lw = cw - g->lx0 - 2 - SB_W;
    if (g->lw < 60) g->lw = 60;

    g->ncw = F->colw[0] + F->colw[1] + F->colw[2] + F->colw[3];
    g->scrolled = g->lw < g->ncw;
    if (!g->scrolled) F->xs = 0;
    int maxxs = g->ncw - g->lw;
    if (F->xs > maxxs) F->xs = maxxs > 0 ? maxxs : 0;
    g->size_x = F->colw[0] - F->xs;
    g->type_x = F->colw[0] + F->colw[1] - F->xs;
    g->mod_x  = F->colw[0] + F->colw[1] + F->colw[2] - F->xs;
    g->list_h = ch - TOP_H - HDR_H - FOOT_H - (g->scrolled ? SB_W : 0);
    if (g->list_h < ROW_H) g->list_h = ROW_H;
    g->vis = g->list_h / ROW_H;

    int maxs = F->nrows - g->vis;
    if (F->scroll > maxs) F->scroll = maxs;
    if (F->scroll < 0) F->scroll = 0;
}

static const char *drow_type(Row *r) { return r->is_dir ? "Folder" : ext_type(r->name); }

static int rowcmp(const Row *a, const Row *b)
{
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    int r = 0;
    switch (F->sort_col) {
    case 1: r = (a->size > b->size) - (a->size < b->size); break;
    case 2: r = strcasecmp(drow_type((Row *)a), drow_type((Row *)b)); break;
    case 3: r = (a->mtime > b->mtime) - (a->mtime < b->mtime); break;
    default: r = strcasecmp(a->name, b->name); break;
    }
    if (r == 0) r = strcasecmp(a->name, b->name);
    return F->sort_desc ? -r : r;
}

static void sort_rows(int from)
{
    for (int i = from + 1; i < F->nrows; i++) {
        Row key = F->rows[i];
        int j = i - 1;
        while (j >= from && rowcmp(&F->rows[j], &key) > 0) { F->rows[j + 1] = F->rows[j]; j--; }
        F->rows[j + 1] = key;
    }
}

static const char *row_disp(const Row *r)
{
    if (F->cur_drive == 0 && F->a_dir[0]) {
        int pl = strlen(F->a_dir);
        if (!strncmp(r->name, F->a_dir, pl) && r->name[pl] == '/')
            return r->name + pl + 1;
    }
    return r->name;
}

static void build_floppy(void)
{
    F->nrows = 0;
    if (!fs_ensure()) return;
    int at_root = !F->a_dir[0];
    if (!at_root) {
        Row *r = &F->rows[F->nrows++];
        strlcpy(r->name, "..", sizeof r->name);
        r->size = 0; r->mtime = 0; r->is_dir = 1; r->fslot = -1;
    }
    for(int i=0;i<FS_NFILES&&F->nrows<MAXROWS;i++){
        FsEnt *e=fs_slot(i);if(!e||!e->used)continue;
        char child[FS_NAMELEN];int kind=fs_child(e->name,F->a_dir,child,sizeof child);
        if(!kind)continue;
        int dir=kind==2||(e->attr&FS_ATTR_DIR),have=0;
        for(int j=0;j<F->nrows;j++)if(!strcmp(F->rows[j].name,child)){have=1;break;}
        if(have)continue;
        Row *r=&F->rows[F->nrows++];strlcpy(r->name,child,sizeof r->name);
        r->size=dir?0:e->size;r->mtime=e->mtime;r->is_dir=dir;r->fslot=dir?-1:i;
    }
    sort_rows(at_root ? 0 : 1);
}

static int build_usb(void)
{
    int n = fat_list(F->cur_path, fe_scratch, MAXROWS - 1);
    int act = ls_action(n, usb_present());
    if (act < 0) return 0;
    F->nrows = 0;
    int at_root = (F->cur_path[0] == '/' && F->cur_path[1] == 0);
    if (!at_root) {
        Row *r = &F->rows[F->nrows++];
        strlcpy(r->name, "..", sizeof r->name);
        r->size = 0; r->mtime = 0; r->is_dir = 1; r->fslot = -1;
    }
    if (act == 1)
        for (int i = 0; i < n && F->nrows < MAXROWS; i++) {
            Row *r = &F->rows[F->nrows++];
            strlcpy(r->name, fe_scratch[i].name, sizeof r->name);
            r->size = fe_scratch[i].size;
            r->mtime = fe_scratch[i].mtime;
            r->is_dir = fe_scratch[i].is_dir;
            r->fslot = -1;
        }
    sort_rows(at_root ? 0 : 1);
    return 1;
}

static void refresh(void)
{
    int done = 1;
    if (F->cur_drive == 0) build_floppy();
    else done = build_usb();
    F->need_refresh = !done;
    F->refresh_t = ticks;
    if (F->scroll > F->nrows) F->scroll = 0;
}

static void stale_check(void)
{
    if (F->cur_drive != 0 || F->renaming) return;
    if ((u32)(ticks - F->refresh_t) < 50) return;
    F->refresh_t = ticks;
    F->need_refresh = 1;
}

static int obj_count(void)
{
    int c = 0;
    for (int i = 0; i < F->nrows; i++)
        if (strcmp(F->rows[i].name, "..")) c++;
    return c;
}

static int at_root(void)
{
    if (F->cur_drive == 0) return !F->a_dir[0];
    return F->cur_path[0] == '/' && F->cur_path[1] == 0;
}

static int find_sel(void)
{
    const char *f = sel_focus();
    if (!f[0]) return -1;
    for (int i = 0; i < F->nrows; i++)
        if (!strcmp(F->rows[i].name, f)) return i;
    return -1;
}

static void nav_apply_fields(int drive, const char *adir, const char *path)
{
    F->cur_drive = drive;
    strlcpy(F->a_dir, adir, sizeof F->a_dir);
    strlcpy(F->cur_path, path, sizeof F->cur_path);
    sel_clear();
    F->scroll = 0;
    F->xs = 0;
    F->need_refresh = 1;
}
static int loc_eq(int drive, const char *adir, const char *path)
{
    return F->cur_drive == drive && !strcmp(F->a_dir, adir) && !strcmp(F->cur_path, path);
}
static void loc_capture(Loc *l)
{
    l->drive = (u8)F->cur_drive;
    strlcpy(l->a_dir, F->a_dir, sizeof l->a_dir);
    strlcpy(l->path, F->cur_path, sizeof l->path);
}
static void loc_restore(Loc *l) { nav_apply_fields(l->drive, l->a_dir, l->path); }

static void nav_to(int drive, const char *adir, const char *path)
{
    if (loc_eq(drive, adir, path)) return;
    nav_apply_fields(drive, adir, path);
    if (F->hist_i < F->hist_n - 1) F->hist_n = F->hist_i + 1;
    if (F->hist_n >= HISTMAX) {
        memmove(&F->hist[0], &F->hist[1], (HISTMAX - 1) * sizeof(Loc));
        F->hist_n = HISTMAX - 1;
        if (F->hist_i > 0) F->hist_i--;
    }
    loc_capture(&F->hist[F->hist_n]);
    F->hist_i = F->hist_n;
    F->hist_n++;
}
static void nav_back(void) { if (F->hist_i > 0) { F->hist_i--; loc_restore(&F->hist[F->hist_i]); } }
static void nav_fwd(void)  { if (F->hist_i < F->hist_n - 1) { F->hist_i++; loc_restore(&F->hist[F->hist_i]); } }
static void nav_seed(void) { F->hist_n = 0; loc_capture(&F->hist[0]); F->hist_i = 0; F->hist_n = 1; }

static void act_up(void)
{
    if (at_root()) return;
    if (F->cur_drive == 0) { char p[FS_NAMELEN];strlcpy(p,F->a_dir,sizeof p);int cut=0;for(int i=0;p[i];i++)if(p[i]=='/')cut=i;p[cut]=0;nav_to(0,p,"/");return; }
    char np[128];
    strlcpy(np, F->cur_path, sizeof np);
    int slash = -1;
    for (int k = 0; np[k]; k++) if (np[k] == '/') slash = k;
    np[slash > 0 ? slash : 1] = 0;
    nav_to(1, "", np);
}

static void switch_drive(int d)
{
    if (d == F->cur_drive) return;
    if (d == 1 && !usb_present()) return;
    nav_to(d, "", "/");
    F->btnfocus = -1;
    F->fm_msg[0] = 0;
}

static void fileman_open(int inst)
{
    F = &fms[inst];
    memset(F, 0, sizeof *F);
    strlcpy(F->cur_path, "/", sizeof F->cur_path);
    F->cliW = dcliW;
    F->cliH = dcliH;
    F->btnfocus = -1;

    F->colw[1] = COL_SIZE;
    F->colw[2] = COL_TYPE;
    F->colw[3] = COL_MOD;
    F->colw[0] = (dcliW - (SIDEBAR + 2) - 2 - SB_W) - (COL_SIZE + COL_TYPE + COL_MOD);
    if (F->colw[0] < 120) F->colw[0] = 120;
    F->need_refresh = 1;
    F->seen_gen = api->usb_gen();
    nav_seed();
}

static void fm_check_usb(void)
{
    u32 g = api->usb_gen();
    if (g == F->seen_gen) return;
    F->seen_gen = g;
    if (F->cur_drive == 1) {
        strlcpy(F->cur_path, "/", sizeof F->cur_path);
        F->nsel = 0;
        F->scroll = 0;
        F->a_dir[0] = 0;
        F->fm_msg[0] = 0;
        F->need_refresh = 1;
    }
}

static void name_spec(const char *nm, char *dst, int cap)
{
    if (F->cur_drive == 0) kfmt(dst, cap, "a:%s", nm);
    else if (strlen(F->cur_path) > 1) kfmt(dst, cap, "u:%s/%s", F->cur_path, nm);
    else kfmt(dst, cap, "u:/%s", nm);
}

static int sel_specs(char *dst, int cap, int *dropped)
{
    int o = 0, count = 0, lost = 0;
    dst[0] = 0;
    for (int i = 0; i < F->nsel; i++) {
        const char *nm = F->sel[i];
        if (!strcmp(nm, "..")) continue;
        int isdir = 0;
        for (int r = 0; r < F->nrows; r++)
            if (!strcmp(F->rows[r].name, nm)) { isdir = F->rows[r].is_dir; break; }
        char spec[200];
        name_spec(nm, spec, sizeof spec);
        if (isdir) {
            int s2 = strlen(spec);
            if (s2 < (int)sizeof spec - 1) { spec[s2] = '/'; spec[s2 + 1] = 0; }
        }
        int sl = strlen(spec);
        if (o + sl + 2 > cap) { lost++; continue; }
        if (o) dst[o++] = '\n';
        memcpy(dst + o, spec, sl);
        o += sl;
        dst[o] = 0;
        count++;
    }
    if (dropped) *dropped = lost;
    return count;
}

static int list_has(const char *list, const char *spec)
{
    const char *p = list;
    int sl = strlen(spec);
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        if (e - p == sl && !strncmp(p, spec, sl)) return 1;
        if (!*e) break;
        p = e + 1;
    }
    return 0;
}
static int is_cut_row(const Row *r)
{
    if (!g_cut || !strcmp(r->name, "..")) return 0;
    char spec[200];
    name_spec(r->name, spec, sizeof spec);
    if (r->is_dir) {
        int sl = strlen(spec);
        if (sl < (int)sizeof spec - 1) { spec[sl] = '/'; spec[sl + 1] = 0; }
    }
    return list_has(g_cut_list, spec);
}

static void open_row(int i)
{
    if (i < 0 || i >= F->nrows) return;
    Row *r = &F->rows[i];
    if (r->is_dir) {
        if (F->cur_drive == 0) {
            char nd[FS_NAMELEN];
            if (!strcmp(r->name, "..")) { act_up();return; }
            else strlcpy(nd, r->name, sizeof nd);
            nav_to(0, nd, "/");
            return;
        }
        char np[128];
        if (!strcmp(r->name, "..")) {
            strlcpy(np, F->cur_path, sizeof np);
            int slash = -1;
            for (int k = 0; np[k]; k++) if (np[k] == '/') slash = k;
            np[slash > 0 ? slash : 1] = 0;
        } else {
            int len = strlen(F->cur_path);
            strlcpy(np, F->cur_path, sizeof np);
            if (len > 1 && np[len - 1] != '/') np[len++] = '/';
            strlcpy(np + len, r->name, sizeof np - len);
        }
        nav_to(1, "", np);
        return;
    }
    if (r->size > 256 * 1024) {
        strlcpy(F->fm_msg, "too big to open (256 KB max)", sizeof F->fm_msg);
        return;
    }

    api->busy_set("Opening", r->name, -1);
    if (F->cur_drive == 0) {
        int n = fs_read(r->name, iobuf, IOBUF_SZ);
        if (n < 0) strlcpy(F->fm_msg, "read failed", sizeof F->fm_msg);
        else if (opener_dispatch(r->name, 0, iobuf, n) != 0)
            strlcpy(F->fm_msg, "no app for this file", sizeof F->fm_msg);
    } else {
        char full[192];
        int pl = strlen(F->cur_path);
        if (pl > 1) kfmt(full, sizeof full, "%s/%s", F->cur_path, r->name);
        else kfmt(full, sizeof full, "/%s", r->name);
        int len = fat_read(full, iobuf, IOBUF_SZ);
        if (len < 0) strlcpy(F->fm_msg, "read failed", sizeof F->fm_msg);
        else {
            char nm[64], fp[192];
            strlcpy(nm, r->name, sizeof nm);
            strlcpy(fp, full, sizeof fp);
            if (opener_dispatch(nm, fp, iobuf, len) != 0)
                strlcpy(F->fm_msg, "no app for this file", sizeof F->fm_msg);
        }
    }
    api->busy_end();
}

static void begin_rename(void);

static void act_new(void)
{
    if (F->cur_drive != 0) return;
    int e = api->app_find("Editor");
    if (e >= 0) win_open(e);
    else strlcpy(F->fm_msg, "no editor loaded", sizeof F->fm_msg);
}

static void act_newdir(void)
{
    char nm[FS_NAMELEN];
    int r;

    if (F->cur_drive != 0) {

        if (!api->fat_can_mkdir() || !fat_writable()) {
            strlcpy(F->fm_msg, "USB is not writable", sizeof F->fm_msg);
            return;
        }
        char full[128];
        int base_at_root = (F->cur_path[0] == '/' && !F->cur_path[1]);
        for (int n = 0; n < 100; n++) {
            if (n == 0) strlcpy(nm, "NEWDIR", sizeof nm);
            else        kfmt(nm, sizeof nm, "NEWDIR%d", n);
            if (base_at_root) kfmt(full, sizeof full, "/%s", nm);
            else              kfmt(full, sizeof full, "%s/%s", F->cur_path, nm);
            r = api->fat_mkdir(full);
            if (r == 0) break;
            if (r == -2) { strlcpy(F->fm_msg, "USB full", sizeof F->fm_msg); return; }
        }
        if (r != 0) { strlcpy(F->fm_msg, "could not create", sizeof F->fm_msg); return; }

        for (int i = 0; nm[i]; i++)
            if (nm[i] >= 'A' && nm[i] <= 'Z') nm[i] += 32;
        sel_set_single(nm);
        F->need_refresh = 1;
        strlcpy(F->fm_msg, "folder created", sizeof F->fm_msg);
        return;
    }
    char base[16];
    for(int n=1;n<100;n++){
        kfmt(base,sizeof base,n==1?"New folder":"Folder %d",n);
        if(strlen(F->a_dir)+(F->a_dir[0]?1:0)+strlen(base)>=sizeof nm){strlcpy(F->fm_msg,"Folder path is too long",sizeof F->fm_msg);return;}
        kfmt(nm,sizeof nm,F->a_dir[0]?"%s/%s":"%s%s",F->a_dir,base);
        if(!fs_exists(nm))break;
    }
    r = api->fs_mkdir(nm);
    if (r != 0) {
        strlcpy(F->fm_msg, r == -2 ? "directory full" : "could not create",
                sizeof F->fm_msg);
        return;
    }
    refresh();
    sel_set_single(nm);
    begin_rename();
}

static int del_one(const char *nm, int isdir)
{
    if (F->cur_drive == 0) {
        if (!isdir) return fs_delete(nm);

        if (api->fs_dir_count(nm) > 0) return -3;
        if (!api->fs_is_dir(nm)) return -1;
        return fs_delete(nm);
    }
    char full[128];
    if (F->cur_path[0] == '/' && !F->cur_path[1])
        kfmt(full, sizeof full, "/%s", nm);
    else
        kfmt(full, sizeof full, "%s/%s", F->cur_path, nm);
    return isdir ? api->fat_rmdir(full) : fat_delete(full);
}

static int row_is_dir(const char *nm)
{
    for (int r = 0; r < F->nrows; r++)
        if (!strcmp(F->rows[r].name, nm)) return F->rows[r].is_dir;
    return 0;
}

static void del_confirmed(int result, void *ctx)
{

    if (ctx) F = (Fm *)ctx;
    if (result != MBR_YES) return;
    int done = 0, busy = 0, failed = 0;

    int total = F->nsel;
    for (int i = 0; i < F->nsel; i++) {
        const char *nm = F->sel[i];
        if (!strcmp(nm, "..")) continue;
        api->busy_set("Deleting", nm, total ? i * 256 / total : 0);
        int r = del_one(nm, row_is_dir(nm));
        if (r == 0) done++;
        else if (r == -3) busy++;
        else failed++;
    }
    api->busy_end();

    if (F->cur_drive == 0 && F->a_dir[0] &&
        api->fs_dir_count(F->a_dir) == 0 && !api->fs_is_dir(F->a_dir))
        api->fs_mkdir(F->a_dir);
    sel_clear();
    F->need_refresh = 1;
    if (busy)        strlcpy(F->fm_msg, "folder not empty", sizeof F->fm_msg);
    else if (failed) strlcpy(F->fm_msg, "delete failed", sizeof F->fm_msg);
    else if (done)   kfmt(F->fm_msg, sizeof F->fm_msg, "deleted %d item%s",
                          done, done == 1 ? "" : "s");
}

static void act_delete(void)
{
    if (F->cur_drive != 0 && (!api->fat_can_mkdir() || !fat_writable())) {
        strlcpy(F->fm_msg, "USB is not writable", sizeof F->fm_msg);
        return;
    }
    int nfiles = 0, nfolders = 0;
    const char *first = 0;
    for (int i = 0; i < F->nsel; i++) {
        if (!strcmp(F->sel[i], "..")) continue;
        if (row_is_dir(F->sel[i])) nfolders++; else nfiles++;
        if (!first) first = F->sel[i];
    }
    if (!nfiles && !nfolders) return;

    char q[96];
    del_prompt(q, sizeof q, nfiles, nfolders, first);
    api->msgbox("Delete", q, MB_YESNO, del_confirmed, F);
}

static void act_duplicate(void)
{
    if (F->cur_drive != 0) return;
    int i = find_sel();
    if (i < 0 || !strcmp(F->rows[i].name, "..")) return;

    Fm *target = F;
    char spec[FS_NAMELEN + 4], dir[FS_NAMELEN], dst[96];
    kfmt(spec, sizeof spec, "a:%s%s", F->rows[i].name,
         F->rows[i].is_dir ? "/" : "");
    strlcpy(dir, F->a_dir, sizeof dir);
    int moved, r = ft_transfer(api, spec, 0, dir, FT_COPY, dst, &moved);
    F = target;
    F->need_refresh = 1;
    if (!r) { sel_set_single(dst); strlcpy(F->fm_msg, "Copy created", sizeof F->fm_msg); }
    else strlcpy(F->fm_msg, ft_reason(r), sizeof F->fm_msg);
}

static void begin_rename(void)
{
    if (F->cur_drive != 0 && !api->fat_can_mkdir()) return;
    int i = find_sel();
    if (i < 0) return;
    F->renaming = 1;
    strlcpy(F->rnbuf, row_disp(&F->rows[i]), sizeof F->rnbuf);
    F->rnlen = strlen(F->rnbuf);
    F->rncar = F->rnlen;

    F->rnall = 1;
}
static void commit_rename(void)
{
    F->renaming = 0;
    int i = find_sel();
    if (i < 0 || !F->rnlen) return;
    if (!strcmp(F->rows[i].name, "..")) return;

    if (F->cur_drive != 0) {

        char full[128];
        if (F->cur_path[0] == '/' && !F->cur_path[1])
            kfmt(full, sizeof full, "/%s", F->rows[i].name);
        else
            kfmt(full, sizeof full, "%s/%s", F->cur_path, F->rows[i].name);
        if (api->fat_rename(full, F->rnbuf) == 0) {
            sel_set_single(F->rnbuf);
            F->need_refresh = 1;
        } else strlcpy(F->fm_msg, "rename failed", sizeof F->fm_msg);
        return;
    }

    if (F->rows[i].is_dir) {
        char dst[FS_NAMELEN];
        if(strlen(F->a_dir)+(F->a_dir[0]?1:0)+strlen(F->rnbuf)>=sizeof dst){strlcpy(F->fm_msg,"Path is too long",sizeof F->fm_msg);return;}
        kfmt(dst,sizeof dst,F->a_dir[0]?"%s/%s":"%s%s",F->a_dir,F->rnbuf);
        int r = api->fs_rename_dir(F->rows[i].name, dst);
        if (r == 0) {
            sel_set_single(dst);
            F->need_refresh = 1;
        } else strlcpy(F->fm_msg,
                       r == -2 ? "name too long for contents" : "rename failed",
                       sizeof F->fm_msg);
        return;
    }

    char full[FS_NAMELEN];
    if(strlen(F->a_dir)+(F->a_dir[0]?1:0)+strlen(F->rnbuf)>=sizeof full){strlcpy(F->fm_msg,"Path is too long",sizeof F->fm_msg);return;}
    if (F->a_dir[0]) kfmt(full, sizeof full, "%s/%s", F->a_dir, F->rnbuf);
    else strlcpy(full, F->rnbuf, sizeof full);
    if (!strcmp(full, F->rows[i].name)) return;
    if (fs_exists(full)) { strlcpy(F->fm_msg, "name exists", sizeof F->fm_msg); return; }

    if (api->fs_rename(F->rows[i].name, full) == 0) {
        sel_set_single(full);
        F->need_refresh = 1;
    } else strlcpy(F->fm_msg, "rename failed", sizeof F->fm_msg);
}

static void copy_list_to(const char *list,int drive,const char *adir,const char *upath,int mode)
{
    Fm *target=F;char dir[128];strlcpy(dir,drive?upath:adir,sizeof dir);
    ft_batch(api,list,drive,dir,mode,&transfers);
    F=target;F->need_refresh=1;ft_message(api,&transfers,F->fm_msg,sizeof F->fm_msg);
}

static void say_count(const char *verb, int n, int dropped)
{
    if (dropped)
        kfmt(F->fm_msg, sizeof F->fm_msg, "%d %s - %d too many to fit",
             n, verb, dropped);
    else
        kfmt(F->fm_msg, sizeof F->fm_msg, "%d file%s %s", n, n == 1 ? "" : "s", verb);
}

static void act_copy(void)
{
    int dropped = 0;
    int n = sel_specs(g_specs, sizeof g_specs, &dropped);
    if (!n) { strlcpy(F->fm_msg, "nothing to copy", sizeof F->fm_msg); return; }

    if (api->clip_set("file", g_specs, strlen(g_specs) + 1) != 0) {
        strlcpy(F->fm_msg, "selection too large for the clipboard",
                sizeof F->fm_msg);
        return;
    }
    g_cut = 0; g_cut_list[0] = 0;
    say_count("copied", n, dropped);
}

static void act_cut(void)
{
    int dropped=0,n=sel_specs(g_specs,sizeof g_specs,&dropped);
    if(!n){strlcpy(F->fm_msg,"Select files to cut",sizeof F->fm_msg);return;}
    if(api->clip_set("file.cut",g_specs,strlen(g_specs)+1)){
        strlcpy(F->fm_msg,"Selection is too large for clipboard",sizeof F->fm_msg);return;
    }
    say_count("cut",n,dropped);
}
static void act_paste(void)
{
    int mode;
    if(!ft_clip_get(api,g_specs,&mode)){strlcpy(F->fm_msg,"No files on clipboard",sizeof F->fm_msg);return;}
    copy_list_to(g_specs,F->cur_drive,F->a_dir,F->cur_path,mode);
    if(mode==FT_MOVE)ft_clip_finish(api,g_specs,&transfers);
}

static void act_selall(void)
{
    sel_clear();
    for (int i = 0; i < F->nrows; i++)
        if (strcmp(F->rows[i].name, "..")) sel_add(F->rows[i].name);
}

static void addr_str(char *out, int cap);
static void act_props(void)
{
    int i=find_sel();
    if (i<0) return;
    Row *r=&F->rows[i];
    char ab[128]; addr_str(ab,sizeof ab);
    fp_show(row_disp(r),drow_type(r),r->size,r->mtime,ab,r->is_dir);
}

static int ends_ku(const char *s)
{
    int l = strlen(s);
    return l > 3 && !strcasecmp(s + l - 3, ".ku");
}
static void act_install(void)
{
    int i = find_sel();
    if (i < 0) return;
    char err[48];
    int rc;
    if (F->cur_drive == 0) {
        rc = api->kernel_update(F->rows[i].name, err, sizeof err);
    } else {
        char full[192];
        if (strlen(F->cur_path) > 1)
            kfmt(full, sizeof full, "%s/%s", F->cur_path, F->rows[i].name);
        else kfmt(full, sizeof full, "/%s", F->rows[i].name);
        int n = fat_read(full, iobuf, IOBUF_SZ);
        if (n < 0) { strlcpy(F->fm_msg, "USB read failed", sizeof F->fm_msg); return; }
        rc = api->kernel_update_data(iobuf, (u32)n, err, sizeof err);
    }
    if (rc != 0) strlcpy(F->fm_msg, err, sizeof F->fm_msg);
}

static void copy_path(void)
{
    int dropped=0,n=sel_specs(g_specs,sizeof g_specs,&dropped);
    if(n>0){api->clip_set_text(g_specs);strlcpy(F->fm_msg,dropped?"Some paths did not fit in the clipboard":"File path copied",sizeof F->fm_msg);}
}

static void do_action(int a)
{
    switch (a) {
    case MA_OPEN:    open_row(find_sel()); break;
    case MA_CUT:     act_cut(); break;
    case MA_COPY:    act_copy(); break;
    case MA_PATH:    copy_path(); break;
    case MA_PASTE:   act_paste(); break;
    case MA_RENAME:  begin_rename(); break;
    case MA_DUP:     act_duplicate(); break;
    case MA_DELETE:  act_delete(); break;
    case MA_PROPS:   act_props(); break;
    case MA_INSTALL: act_install(); break;
    case MA_NEW:     act_new(); break;
    case MA_NEWDIR:  act_newdir(); break;
    case MA_REFRESH: F->need_refresh = 1; strlcpy(F->fm_msg, "refreshed", sizeof F->fm_msg); break;
    case MA_CLOSE:   win_close_self(files_type, (int)(F - fms)); break;
    case MA_BACK:    nav_back(); break;
    case MA_FWD:     nav_fwd(); break;
    case MA_UP:      act_up(); break;
    case MA_DRIVEA:  switch_drive(0); break;
    case MA_DRIVEU:  switch_drive(1); break;
    case MA_SORTN:   F->sort_col = 0; F->sort_desc = 0; F->need_refresh = 1; break;
    case MA_SORTS:   F->sort_col = 1; F->sort_desc = 0; F->need_refresh = 1; break;
    case MA_SORTT:   F->sort_col = 2; F->sort_desc = 0; F->need_refresh = 1; break;
    case MA_SORTD:   F->sort_col = 3; F->sort_desc = 1; F->need_refresh = 1; break;
    case MA_SELALL:  act_selall(); break;
    case MA_ABOUT:   strlcpy(F->fm_msg, "FLOPNIX Files", sizeof F->fm_msg); break;
    }
}
static void menu_pick(int idx, void *ctx)
{
    F = (Fm *)ctx;
    if (idx < 0 || idx >= (int)sizeof menu_codes) return;
    do_action(menu_codes[idx]);
}

static int tb_w(int i)
{
    int lw = strlen(TB[i].label) * 8;
    return (lw > 16 ? lw : 16) + 16;
}
static int tb_x(int i)
{
    int x = 8;
    for (int k = 0; k < i; k++) { x += tb_w(k) + 4; if (k == 2) x += 8; }
    return x;
}
static void fm_button(int x,int y,int w,int h,const char *label,int selected,int enabled)
{
    int state=enabled?api->control_state(x,y,w,h):0,down=(state&2)!=0;
    u8 bg=selected?C_NAVY:state?C_G0+7:C_FACE;
    fill_rect(x,y,w,h,bg);
    if(selected||state)rect(x,y,w,h,down?C_NAVY:C_SHAD);
    int tx=x+(w-api->text_width(label))/2+down,ty=y+(h-16)/2+down;
    draw_text(tx,ty,label,!enabled?C_G0+3:selected?C_WHITE:C_BLACK);
}

static int clip_is_file(void)
{
    const char *t = api->clip_type();
    char first=0;
    return t && (!strcmp(t,"file") || !strcmp(t,"file.cut")) &&
           api->clip_get(t,&first,1)>0 && first;
}
static int tb_enabled(int i)
{
    switch (TB[i].action) {
    case MA_BACK:  return F->hist_i > 0;
    case MA_FWD:   return F->hist_i < F->hist_n - 1;
    case MA_UP:    return !at_root();
    case MA_CUT:   return F->nsel > 0;
    case MA_COPY:  return F->nsel > 0;
    case MA_PASTE: return clip_is_file();
    }
    return 1;
}

static int menu_w(int i) { return (int)strlen(MENUS[i]) * 8 + MPAD * 2; }
static int menu_x(int i) { int x = 4; for (int k = 0; k < i; k++) x += menu_w(k); return x; }

static void open_menu(int i, int ox, int oy)
{
    (void)ox;(void)oy;
    const char **items=F->menu_items;
    u8 *codes=F->menu_actions;
    int n = 0;
    switch (i) {
    case 0:
        if (F->cur_drive == 0) { items[n] = "New"; codes[n++] = MA_NEW; }
        if (F->cur_drive == 0 ||
            (F->cur_drive != 0 && api->fat_can_mkdir()))
            { items[n] = "New Folder"; codes[n++] = MA_NEWDIR; }
        items[n] = "Open"; codes[n++] = MA_OPEN;
        if (F->cur_drive == 0 || (api->fat_can_mkdir() && fat_writable()))
            { items[n] = "Delete"; codes[n++] = MA_DELETE; }
        items[n] = "Close"; codes[n++] = MA_CLOSE;
        break;
    case 1:
        items[n] = "Cut"; codes[n++] = MA_CUT;
        items[n] = "Copy"; codes[n++] = MA_COPY;
        items[n] = "Paste"; codes[n++] = MA_PASTE;
        items[n] = "Copy filepath"; codes[n++] = MA_PATH;
        items[n] = "Rename"; codes[n++] = MA_RENAME;
        if (F->cur_drive == 0 && find_sel() >= 0)
            { items[n] = "Duplicate"; codes[n++] = MA_DUP; }
        items[n] = "Select All"; codes[n++] = MA_SELALL;
        break;
    case 2:
        items[n] = "Refresh"; codes[n++] = MA_REFRESH;
        items[n] = "by Name"; codes[n++] = MA_SORTN;
        items[n] = "by Size"; codes[n++] = MA_SORTS;
        items[n] = "by Type"; codes[n++] = MA_SORTT;
        items[n] = "by Date"; codes[n++] = MA_SORTD;
        break;
    default:
        items[n] = "Back"; codes[n++] = MA_BACK;
        items[n] = "Forward"; codes[n++] = MA_FWD;
        items[n] = "Up"; codes[n++] = MA_UP;
        items[n] = "Floppy"; codes[n++] = MA_DRIVEA;
        items[n] = "USB Drive"; codes[n++] = MA_DRIVEU;
        break;
    }
    F->menu=i+1;F->menu_sel=-1;F->menu_n=n;
}

static int menu_left(void)
{
    int x=menu_x(F->menu-1);if(x+180>F->cliW)x=F->cliW-180;return x<0?0:x;
}
static void draw_menu(int cx,int cy)
{
    if(!F->menu)return;
    int x=cx+menu_left(),y=cy+MENU_H;
    panel(x,y,180,F->menu_n*20+4,0);
    menu_shade(api,x+2,y+2,176,F->menu_n*20,0);
    for(int i=0;i<F->menu_n;i++){
        int yy=y+2+i*20;
        int hov=F->menu_sel==i||(over&&mx>=x+2&&mx<x+178&&my>=yy&&my<yy+20);
        if(hov)menu_shade(api,x+2,yy,176,20,1);
        draw_text(x+10,yy+2,F->menu_items[i],hov?C_WHITE:C_BLACK);
    }
}
static int menu_input(int x,int y,int ev)
{
    if(!F->menu)return 0;
    if(ev==EV_PRESS&&y>=0&&y<MENU_H){
        for(int i=0;i<NMENU;i++)if(x>=menu_x(i)&&x<menu_x(i)+menu_w(i)){
            if(F->menu==i+1)F->menu=0;else open_menu(i,0,0);return 1;
        }
    }
    if(ev==EV_DRAG){F->menu_sel=-1;return 1;}
    if(ev==EV_PRESS||ev==EV_RPRESS){
        int row=(y-MENU_H-2)/20;
        int hit=x>=menu_left()+2&&x<menu_left()+178&&y>=MENU_H+2&&row<F->menu_n;
        F->menu=0;if(hit&&ev==EV_PRESS)do_action(F->menu_actions[row]);
    }
    return 1;
}

static void open_drive_menu(int ox, int oy_field)
{
    const char *items[2];
    int n = 0;
    items[n] = "Floppy"; menu_codes[n++] = MA_DRIVEA;
    if (usb_present()) { items[n] = "USB Drive"; menu_codes[n++] = MA_DRIVEU; }
    api->menu_show(ox, oy_field, items, n, menu_pick, F);
}

static u8 cc(u8 nat, u8 mono) { return mono ? mono : nat; }

static void draw_icon(int ic, int x, int y, u8 mono)
{
    switch (ic) {
    case IC_BACK: draw_text_scaled(x, y, s_left,  cc(C_NAVY, mono), 2, 1); break;
    case IC_FWD:  draw_text_scaled(x, y, s_right, cc(C_NAVY, mono), 2, 1); break;
    case IC_UP:   draw_text_scaled(x, y, s_up,    cc(C_NAVY, mono), 2, 1); break;
    case IC_CUT:
        circle(x + 4, y + 12, 2, cc(C_GRAY, mono));
        circle(x + 10, y + 12, 2, cc(C_GRAY, mono));
        line(x + 5, y + 11, x + 12, y + 2, cc(C_G0 + 2, mono));
        line(x + 9, y + 11, x + 2, y + 2, cc(C_G0 + 2, mono));
        break;
    case IC_COPY:
        fill_rect(x + 2, y + 2, 8, 10, cc(C_WHITE, mono));
        rect(x + 2, y + 2, 8, 10, cc(C_G0 + 3, mono));
        fill_rect(x + 6, y + 5, 8, 10, cc(C_WHITE, mono));
        rect(x + 6, y + 5, 8, 10, cc(C_NAVY, mono));
        hline(x + 8, y + 8, 4, cc(C_BBLUE, mono));
        hline(x + 8, y + 10, 4, cc(C_BBLUE, mono));
        break;
    case IC_PASTE:
        fill_rect(x + 2, y + 3, 11, 12, cc(C_OLIVE, mono));
        rect(x + 2, y + 3, 11, 12, cc(C_G0 + 1, mono));
        fill_rect(x + 5, y + 1, 5, 3, cc(C_G0 + 2, mono));
        fill_rect(x + 4, y + 5, 7, 8, cc(C_WHITE, mono));
        hline(x + 5, y + 7, 5, cc(C_NAVY, mono));
        hline(x + 5, y + 9, 5, cc(C_NAVY, mono));
        break;
    case IC_FOLDER:
        fill_rect(x + 1, y + 3, 12, 9, cc(C_YELLOW, mono));
        fill_rect(x + 1, y + 1, 6, 3, cc(C_YELLOW, mono));
        rect(x + 1, y + 3, 12, 9, cc(C_OLIVE, mono));
        break;
    case IC_FILE:
        fill_rect(x + 2, y + 1, 9, 12, cc(C_WHITE, mono));
        rect(x + 2, y + 1, 9, 12, cc(C_G0 + 3, mono));
        hline(x + 4, y + 4, 5, cc(C_BBLUE, mono));
        hline(x + 4, y + 6, 5, cc(C_BBLUE, mono));
        hline(x + 4, y + 8, 5, cc(C_BBLUE, mono));
        break;
    case IC_DRIVE:
        fill_rect(x + 1, y + 3, 12, 9, cc(C_G0 + 4, mono));
        rect(x + 1, y + 3, 12, 9, cc(C_G0 + 1, mono));
        fill_rect(x + 3, y + 5, 8, 2, cc(C_G0 + 6, mono));
        break;
    }
}

static void addr_str(char *out, int cap)
{
    if (F->cur_drive == 0) {
        if (F->a_dir[0]) kfmt(out, cap, "Floppy\\%s", F->a_dir);
        else strlcpy(out, "Floppy", cap);
    } else {
        char p[128];
        strlcpy(p, F->cur_path, sizeof p);
        for (char *q = p; *q; q++) if (*q == '/') *q = '\\';
        if (p[0] == '\\' && !p[1]) strlcpy(out, "USB Disk", cap);
        else kfmt(out, cap, "USB Disk%s", p);
    }
}

static void fmt_size(u32 sz, int is_dir, char *out, int cap)
{
    if (is_dir) { strlcpy(out, "<dir>", cap); return; }
    if (sz < 1024) kfmt(out, cap, "%u B", sz);
    else kfmt(out, cap, "%u KB", (sz + 1023) / 1024);
}

static void draw_sidebar(int cx, int cy, int ch)
{
    int sx = cx, sy = cy + TOP_H, sh = ch - TOP_H - FOOT_H;

    fill_rect(sx, sy, SIDEBAR, DRV_H, C_FACE);
    int bw = (SIDEBAR - 9) / 2;
    const char *dl[2] = { "Floppy", "USB" };
    for (int d = 0; d < 2; d++) {
        int bx = sx + 3 + d * (bw + 3), by = sy + 2, bh = DRV_H - 4;
        int avail = (d == 0) || usb_present();
        int cur = d == F->cur_drive;
        fm_button(bx,by,bw,bh,dl[d],cur,avail);
    }

    int gy = sy + DRV_H, band = 30;
    if (band > sh - DRV_H) band = sh - DRV_H;
    fill_rect(sx,gy,SIDEBAR,band,C_G0+7);

    char title[24];
    if (F->cur_drive == 0) strlcpy(title, F->a_dir[0] ? F->a_dir : "Floppy", sizeof title);
    else {
        if (at_root()) {
            const char *lb = fat_label();
            if (lb && lb[0] && strcmp(lb, "none")) strlcpy(title, lb, sizeof title);
            else strlcpy(title, "USB", sizeof title);
        } else {
            const char *b = F->cur_path;
            for (const char *p = F->cur_path; *p; p++) if (*p == '/') b = p + 1;
            strlcpy(title, b, sizeof title);
        }
    }
    draw_icon(F->cur_drive || F->a_dir[0] ? IC_FOLDER : IC_DRIVE, sx + 10, gy + 6, 0);
    draw_text_clip(sx+30,gy+8,title,C_NAVY,SIDEBAR-36);

    int dy0 = gy + band;
    fill_rect(sx, dy0, SIDEBAR - 1, sh - DRV_H - band, C_G0 + 7);
    vline(sx + SIDEBAR - 1, sy, sh, C_SHAD);
    hline(sx + 6, dy0, SIDEBAR - 12, C_G0 + 3);

    int dy = dy0 + 6;
    int i = (F->nsel == 1) ? find_sel() : -1;
    if (i >= 0) {
        Row *r = &F->rows[i];
        if(!r->is_dir&&archive_icon_name(api,r->name))archive_icon(api,sx + 8, dy,1);else draw_icon(r->is_dir ? IC_FOLDER : IC_FILE, sx + 8, dy, 0);
        draw_text_clip(sx + 26, dy, row_disp(r), C_NAVY, SIDEBAR - 32);
        dy += 20;
        draw_text_clip(sx + 10, dy, r->is_dir ? "Folder" : drow_type(r), C_BLACK, SIDEBAR - 16);
        dy += 15;
        if (!r->is_dir) {
            char hs[16], b[28];
            human_size(r->size, hs, sizeof hs);
            kfmt(b, sizeof b, "Size: %s", hs);
            draw_text_clip(sx + 10, dy, b, C_BLACK, SIDEBAR - 16);
            dy += 15;
        }
        char mt[18], mb[28];
        dos_fmt(r->mtime, mt);
        kfmt(mb, sizeof mb, "Mod: %s", mt);
        draw_text_clip(sx + 10, dy, mb, C_G0 + 2, SIDEBAR - 16);
    } else {
        char b[28];
        kfmt(b, sizeof b, "%d items", obj_count());
        draw_text(sx + 10, dy, b, C_BLACK);
        dy += 17;
        if (F->cur_drive == 0) kfmt(b, sizeof b, "%u KB free", fs_free_kb());
        else { char c[16]; human_size_kb(usb_capacity_kb(), c, sizeof c); kfmt(b, sizeof b, "%s disk", c); }
        draw_text(sx + 10, dy, b, C_G0 + 2);
        dy += 22;
        if (F->nsel > 1) {
            kfmt(b, sizeof b, "%d selected", F->nsel);
            draw_text(sx + 10, dy, b, C_NAVY);
        }
    }
}

static void fileman_draw(Win *w, int cx, int cy, int cw, int ch)
{
    Fm *previous=F;
    gfx = gdi_bind(api, 11);
    F = &fms[w->inst];
    fm_check_usb();
    stale_check();
    F->cliW = cw; F->cliH = ch;

    over = api->win_is_hovered(w);
    g_cut=api->clip_get("file.cut",g_cut_list,sizeof g_cut_list)>0;g_cut_list[SPECLIST-1]=0;
    FmGeo g;
    fm_geo(cw, ch, &g);
    if (F->need_refresh) refresh();
    int i_sel = find_sel();

    fill_rect(cx,cy,cw,TOP_H,C_FACE);
    menu_shade(api,cx,cy,cw,MENU_H,0);
    for (int i = 0; i < NMENU; i++) {
        int lxp = cx + menu_x(i), lwp = menu_w(i);
        int hov = over && mx >= lxp && mx < lxp + lwp && my >= cy && my < cy + MENU_H;
        hov=hov||F->menu==i+1;
        if (hov) menu_shade(api,lxp,cy,lwp,MENU_H,1);
        draw_text(lxp + MPAD, cy + 2, MENUS[i], hov ? C_WHITE : C_BLACK);
    }
    hline(cx, cy + MENU_H - 1, cw, C_SHAD);

    for (int i = 0; i < NTB; i++) {
        int bx = cx + tb_x(i), bw = tb_w(i);
        int by = cy + MENU_H + 4, bh = 24;
        int en = tb_enabled(i);
        fm_button(bx,by,bw,bh,TB[i].label,0,en);
        if (F->btnfocus == i && en) focus_rect(bx + 3, by + 3, bw - 6, bh - 6);
        if (i == 2) {
            int sxp = bx + bw + 5;
            vline(sxp, by + 4, bh - 8, C_SHAD);
        }
    }

    int ay = cy + MENU_H + TB_H;
    draw_text(cx + 8, ay + 8, "Address", C_BLACK);
    int fx = cx + ADDR_X, fw = cw - ADDR_X - 8;
    int field_y = ay + 4, fh = 24, dw = 24;
    fill_rect(fx,field_y,fw,fh,C_WHITE);
    rect(fx,field_y,fw,fh,C_SHAD);
    draw_icon(IC_DRIVE,fx+5,field_y+4,0);
    char addr[160];
    addr_str(addr,sizeof addr);
    draw_text_clip(fx+25,field_y+4,addr,C_BLACK,fw-dw-30);
    fm_button(fx+fw-dw,field_y+1,dw-1,fh-2,s_down,0,1);
    vline(fx+fw-dw,field_y+1,fh-2,C_SHAD);
    hline(cx,ay+ADDR_H-1,cw,C_SHAD);

    draw_sidebar(cx, cy, ch);

    int lx0 = cx + g.lx0;
    int clip0 = lx0, clip1 = lx0 + g.lw;
    int size_x = lx0 + g.size_x, type_x = lx0 + g.type_x, mod_x = lx0 + g.mod_x;

    fill_rect(lx0, cy + TOP_H, g.lw + SB_W, HDR_H, C_FACE);
    hline(lx0, cy + TOP_H + HDR_H - 1, g.lw + SB_W, C_SHAD);
    const char *hdr[4] = { "Name", "Size", "Type", "Modified" };
    int hx[4] = { lx0 + 4 - F->xs, size_x, type_x, mod_x };
    for (int c = 0; c < 4; c++) {
        char h[20];
        if (c == F->sort_col) kfmt(h, sizeof h, "%s%s", hdr[c], F->sort_desc ? " v" : " ^");
        else strlcpy(h, hdr[c], sizeof h);
        if (c) vline(hx[c] - 3, cy + TOP_H + 2, HDR_H - 4, C_SHAD);
        draw_text_clip2(hx[c], cy + TOP_H + 1, h, C_NAVY, clip0, clip1);
    }

    int list_y = cy + TOP_H + HDR_H;
    fill_rect(lx0, list_y, g.lw, g.list_h, C_WHITE);
    set_clip(lx0, list_y, g.lw, g.list_h);
    for (int v = 0; v < g.vis; v++) {
        int idx = F->scroll + v;
        if (idx >= F->nrows) break;
        Row *r = &F->rows[idx];
        int ry = list_y + v * ROW_H;
        int selr = row_is_sel(idx);
        int hov = over && mx >= lx0 && mx < lx0 + g.lw && my >= ry && my < ry + ROW_H;
        u8 fg = C_BLACK;
        if (selr) { fill_rect(lx0, ry, g.lw, ROW_H, C_HILITE); fg = C_WHITE; }
        else if (hov) fill_rect(lx0, ry, g.lw, ROW_H, C_G0 + 7);

        int name_x = lx0 + 4 - F->xs;
        if(!r->is_dir&&archive_icon_name(api,r->name))archive_icon(api,name_x, ry + 1,1);else draw_icon(r->is_dir ? IC_FOLDER : IC_FILE, name_x, ry + 1, 0);
        int text_x = name_x + 18;
        int name_end = size_x - 4 < clip1 ? size_x - 4 : clip1;
        if (F->renaming && idx == i_sel) {
            fill_rect(text_x - 2, ry, (name_end - text_x) + 2, ROW_H, C_WHITE);
            if (F->rnall && F->rnlen)
                fill_rect(text_x, ry + 1, F->rnlen * 8, ROW_H - 2, C_NAVY);
            draw_text_clip2(text_x, ry, F->rnbuf,
                            F->rnall ? C_WHITE : C_BLACK, clip0, name_end);

            if (*api->gui_blink && !F->rnall) {
                int cxp = text_x + F->rncar * 8;
                if (cxp < name_end) fill_rect(cxp, ry + 1, 1, ROW_H - 2, C_BLACK);
            }
        } else {
            u8 nfg = selr ? C_WHITE : (is_cut_row(r) ? C_G0 + 4 : (r->is_dir ? C_NAVY : C_BLACK));
            draw_text_clip2(text_x, ry, row_disp(r), nfg, clip0, name_end);
        }
        char sz[16];
        fmt_size(r->size, r->is_dir, sz, sizeof sz);
        draw_text_clip2(size_x + F->colw[1] - 4 - strlen(sz) * 8, ry, sz, fg, clip0, clip1);
        draw_text_clip2(type_x, ry, drow_type(r), fg, clip0,
                        mod_x - 4 < clip1 ? mod_x - 4 : clip1);
        char mt[18];
        dos_fmt(r->mtime, mt);
        draw_text_clip2(mod_x, ry, mt, fg, clip0, clip1);
    }

    set_clip(cx, cy, cw, ch);
    if (F->nrows == 0)
        draw_text(lx0 + 6, list_y + 4, F->cur_drive ? "(empty)" : "(no files - File > New)", C_G0 + 3);

    if (F->band) {
        int ax = F->band_x0 < F->band_cx ? F->band_x0 : F->band_cx;
        int bx = F->band_x0 < F->band_cx ? F->band_cx : F->band_x0;
        int ay = F->band_y0 < F->band_cur ? F->band_y0 : F->band_cur;
        int byr = F->band_y0 < F->band_cur ? F->band_cur : F->band_y0;
        int rx = cx + ax, ry = cy + ay, rw = bx - ax, rh = byr - ay;
        hline(rx, ry, rw, C_NAVY);
        hline(rx, ry + rh, rw, C_NAVY);
        vline(rx, ry, rh, C_NAVY);
        vline(rx + rw, ry, rh, C_NAVY);
    }

    draw_sbar(lx0 + g.lw, list_y, g.list_h, 0, F->nrows, g.vis, F->scroll);
    if (g.scrolled) {
        draw_sbar(lx0, list_y + g.list_h, g.lw, 1, g.ncw, g.lw, F->xs);
        fill_rect(lx0 + g.lw, list_y + g.list_h, SB_W, SB_W, C_FACE);
    }

    int fy = cy + ch - FOOT_H;
    fill_rect(cx, fy, cw, FOOT_H, C_FACE);
    hline(cx, fy, cw, C_LIGHT);
    int p1w = F->fm_msg[0] ? cw : cw * 3 / 5;
    hline(cx,fy,cw,C_SHAD);
    char left[64];
    if (F->fm_msg[0]) strlcpy(left, F->fm_msg, sizeof left);
    else kfmt(left, sizeof left, "%d items", obj_count());
    draw_text_clip(cx + 7, fy + 4, left, C_BLACK, p1w - 12);
    char right[40];
    if (F->cur_drive == 0) kfmt(right, sizeof right, "%u KB free on Floppy", fs_free_kb());
    else kfmt(right, sizeof right, "USB %s", fat_label());
    if(!F->fm_msg[0])draw_text_clip(cx + p1w + 7, fy + 4, right, C_BLACK, cw - p1w - 12);
    draw_menu(cx,cy);
    F=previous;
}

static void fileman_key(int inst, int k)
{
    F = &fms[inst];
    if(F->menu){
        if(k==27)F->menu=0;
        else if(k==K_DOWN)F->menu_sel=(F->menu_sel+1)%F->menu_n;
        else if(k==K_UP)F->menu_sel=F->menu_sel<=0?F->menu_n-1:F->menu_sel-1;
        else if(k==K_LEFT||k==K_RIGHT)open_menu((F->menu-1+(k==K_LEFT?3:1))%4,0,0);
        else if(k=='\n'&&F->menu_sel>=0){int a=F->menu_actions[F->menu_sel];F->menu=0;do_action(a);}
        return;
    }
    if (F->renaming) {
        if (k == '\n') { commit_rename(); return; }
        if (k == 27)   { F->renaming = 0;  return; }
        int rctrl = api->kbd_mods() & 2;
        if (rctrl && kb_unctrl(k, 1) == 'a') {
            F->rnall = 1;
            return;
        }

        int printable = k >= 32 && k < 127 && k != '/' && k != '\\';
        TextField t = { F->rnbuf, FS_NAMELEN, F->rnlen, F->rncar, F->rnall };
        tf_key(&t, k, printable);
        F->rnlen = t.len; F->rncar = t.caret; F->rnall = t.all;
        return;
    }
    if (F->need_refresh) refresh();

    int ctrl = api->kbd_mods() & 2;

    if (ctrl) {
        int c = kb_unctrl(k, 1);
        if (c == 'c') { if(api->kbd_mods()&1)copy_path();else act_copy(); return; }
        if (c == 'x') { act_cut();    return; }
        if (c == 'v') { act_paste();  return; }
        if (c == 'a') { act_selall(); return; }
    }
    if (k == '\b') { act_up(); return; }

    if (k == '\t') {
        F->btnfocus++;
        if (F->btnfocus >= NTB) F->btnfocus = -1;
        return;
    }
    if (k == 27) { F->btnfocus = -1; return; }
    if (k == '\n' && F->btnfocus >= 0) {
        if (tb_enabled(F->btnfocus)) do_action(TB[F->btnfocus].action);
        return;
    }

    int i = find_sel();
    switch (k) {
    case K_UP:   if (i > 0) sel_set_single(F->rows[i - 1].name);
                 else if (F->nrows) sel_set_single(F->rows[0].name); break;
    case K_DOWN: if (i >= 0 && i < F->nrows - 1) sel_set_single(F->rows[i + 1].name);
                 else if (i < 0 && F->nrows) sel_set_single(F->rows[0].name); break;
    case '\n':   open_row(i); break;
    case K_DEL:  act_delete(); break;
    }
    i = find_sel();
    FmGeo g;
    fm_geo(F->cliW, F->cliH, &g);
    if (i >= 0) {
        if (i < F->scroll) F->scroll = i;
        if (i >= F->scroll + g.vis) F->scroll = i - g.vis + 1;
    }
}

static void fileman_wheel(int inst, int dz)
{
    F = &fms[inst];
    if (F->need_refresh) refresh();
    FmGeo g;
    fm_geo(F->cliW, F->cliH, &g);
    F->scroll -= dz * 3;
    int maxs = F->nrows - g.vis;
    if (maxs < 0) maxs = 0;
    if (F->scroll > maxs) F->scroll = maxs;
    if (F->scroll < 0) F->scroll = 0;
}

static int sidebar_drive_hit(int lx, int ly)
{
    int sy = TOP_H;
    if (ly < sy + 2 || ly >= sy + DRV_H - 2) return -1;
    int bw = (SIDEBAR - 9) / 2;
    for (int d = 0; d < 2; d++) {
        int bx = 3 + d * (bw + 3);
        if (lx >= bx && lx < bx + bw) return d;
    }
    return -1;
}

static void fileman_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    F = &fms[inst];
    F->cliW = cw; F->cliH = ch;
    int ox = mx - lx, oy = my - ly;
    if(menu_input(lx,ly,ev))return;
    FmGeo g;
    fm_geo(cw, ch, &g);
    int list_top = TOP_H + HDR_H;

    if (F->sbdrag && ev != EV_PRESS) {
        if (ev == EV_RELEASE) { F->sbdrag = 0; return; }
        if (F->sbdrag == 1)
            F->scroll = sbar_from_pos(g.list_h, F->nrows, g.vis, ly - list_top);
        else
            F->xs = sbar_from_pos(g.lw, g.ncw, g.lw, lx - g.lx0);
        return;
    }
    if (F->coldrag && ev != EV_PRESS) {
        if (ev == EV_RELEASE) { F->coldrag = 0; return; }
        if (ev == EV_DRAG) {
            int d = F->coldrag - 1;
            int left = 0;
            for (int k = 0; k < d; k++) left += F->colw[k];
            int wnew = (lx - g.lx0 + F->xs) - left;
            if (wnew < 24) wnew = 24;
            if (wnew > 400) wnew = 400;
            F->colw[d] = wnew;
        }
        return;
    }
    if (ev == EV_DRAG) {
        if (F->band) {
            int y1 = ly;
            if (y1 < list_top) y1 = list_top;
            if (y1 > list_top + g.list_h) y1 = list_top + g.list_h;
            F->band_cur = y1;
            int x1 = lx;
            if (x1 < g.lx0) x1 = g.lx0;
            if (x1 > g.lx0 + g.lw) x1 = g.lx0 + g.lw;
            F->band_cx = x1;
            int a = F->band_y0, b = y1;
            if (a > b) { int t = a; a = b; b = t; }
            int v0 = (a - list_top) / ROW_H, v1 = (b - list_top) / ROW_H;
            sel_clear();
            for (int v = v0; v <= v1; v++) {
                int idx = F->scroll + v;
                if (idx < 0 || idx >= F->nrows) continue;
                if (!strcmp(F->rows[idx].name, "..")) continue;
                sel_add(F->rows[idx].name);
            }
            return;
        }
        if (F->press_row && !F->drag_sent && !api->drag_active()) {
            int dx = lx - F->press_lx, dy = ly - F->press_ly;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy > 8) {
                int dropped = 0;
                if (sel_specs(g_specs, sizeof g_specs, &dropped) > 0)
                    api->drag_start("file", g_specs);
                if (dropped)
                    kfmt(F->fm_msg, sizeof F->fm_msg,
                         "dragging all but %d - too many to fit", dropped);
                F->drag_sent = 1;
            }
        }
        return;
    }
    if (ev == EV_RELEASE) {
        if (F->click_collapse && !F->drag_sent)
            sel_set_single(F->collapse_name);
        F->click_collapse = 0;
        F->press_row = 0;
        F->band = 0;
        return;
    }
    if (ev == EV_RPRESS) {
        if (F->need_refresh) refresh();
        if (lx < SIDEBAR || ly < list_top || ly >= list_top + g.list_h ||
            lx >= g.lx0 + g.lw) return;
        int row = (ly - list_top) / ROW_H + F->scroll;
        const char *items[12];
        static char copylbl[16], dellbl[16];
        int n = 0;
        if (row >= 0 && row < F->nrows && strcmp(F->rows[row].name, "..")) {
            Row *r = &F->rows[row];
            if (!sel_has(r->name)) sel_set_single(r->name);
            int multi = F->nsel > 1;
            if (multi) kfmt(copylbl, sizeof copylbl, "Copy (%d)", F->nsel);
            else       strlcpy(copylbl, "Copy", sizeof copylbl);
            kfmt(dellbl, sizeof dellbl, multi ? "Delete (%d)" : "Delete", F->nsel);
            items[n] = "Open"; menu_codes[n++] = MA_OPEN;
            items[n] = "Copy filepath"; menu_codes[n++] = MA_PATH;
            if (r->is_dir) {

                if (!multi && strcmp(r->name, "..")) {
                    if (F->cur_drive == 0) {
                        items[n] = "Rename"; menu_codes[n++] = MA_RENAME;
                        items[n] = "Delete"; menu_codes[n++] = MA_DELETE;
                    } else if (api->fat_can_mkdir() && fat_writable()) {
                        items[n] = "Rename"; menu_codes[n++] = MA_RENAME;
                        items[n] = "Delete"; menu_codes[n++] = MA_DELETE;
                    }
                }
                items[n] = "Properties"; menu_codes[n++] = MA_PROPS;
                items[n] = "Refresh"; menu_codes[n++] = MA_REFRESH;
            } else {
                if (!multi && ends_ku(r->name))
                    { items[n] = "Install kernel"; menu_codes[n++] = MA_INSTALL; }
                items[n] = "Cut"; menu_codes[n++] = MA_CUT;
                items[n] = copylbl; menu_codes[n++] = MA_COPY;
                items[n] = "Paste"; menu_codes[n++] = MA_PASTE;
                if (F->cur_drive == 0) {
                    if (!multi) { items[n] = "Rename"; menu_codes[n++] = MA_RENAME; }
                    items[n] = dellbl; menu_codes[n++] = MA_DELETE;
                } else if (api->fat_can_mkdir() && fat_writable()) {
                    if (!multi) { items[n] = "Rename"; menu_codes[n++] = MA_RENAME; }
                    items[n] = dellbl; menu_codes[n++] = MA_DELETE;
                }
                if (!multi && n < 12) { items[n] = "Properties"; menu_codes[n++] = MA_PROPS; }
            }
        } else {
            items[n] = "Paste"; menu_codes[n++] = MA_PASTE;
            if (F->cur_drive == 0) { items[n] = "New file"; menu_codes[n++] = MA_NEW; }
            if (F->cur_drive == 0 ||
                (F->cur_drive != 0 && api->fat_can_mkdir()))
                { items[n] = "New Folder"; menu_codes[n++] = MA_NEWDIR; }
            items[n] = "Refresh"; menu_codes[n++] = MA_REFRESH;
        }
        api->menu_show(mx, my, items, n, menu_pick, F);
        return;
    }
    if (ev != EV_PRESS) return;
    F->press_row = 0;
    F->fm_msg[0] = 0;

    if (ly < MENU_H) {
        for (int i = 0; i < NMENU; i++)
            if (lx >= menu_x(i) && lx < menu_x(i) + menu_w(i)) { open_menu(i, ox, oy); return; }
        return;
    }

    if (ly < MENU_H + TB_H) {
        for (int i = 0; i < NTB; i++) {
            int bx = tb_x(i), bw = tb_w(i);
            if (ly >= MENU_H+4 && ly < MENU_H+TB_H-4 && lx >= bx && lx < bx + bw && tb_enabled(i)) { do_action(TB[i].action); return; }
        }
        return;
    }

    if (ly < TOP_H) {
        int dx = cw - 8 - 24;
        if (ly >= MENU_H+TB_H+5 && ly < TOP_H-5 && lx >= dx && lx < dx+23)
            open_drive_menu(ox+dx,oy+TOP_H);
        return;
    }

    if (F->need_refresh) refresh();

    if (lx >= g.lx0 + g.lw && lx < g.lx0 + g.lw + SB_W &&
        ly >= list_top && ly < list_top + g.list_h) {
        F->sbdrag = 1;
        F->scroll = sbar_from_pos(g.list_h, F->nrows, g.vis, ly - list_top);
        return;
    }

    if (g.scrolled && ly >= list_top + g.list_h && ly < list_top + g.list_h + SB_W &&
        lx >= g.lx0) {
        F->sbdrag = 2;
        F->xs = sbar_from_pos(g.lw, g.ncw, g.lw, lx - g.lx0);
        return;
    }

    if (lx < SIDEBAR) {
        int d = sidebar_drive_hit(lx, ly);
        if (d >= 0) switch_drive(d);
        return;
    }

    if (ly < list_top) {
        int vrel = lx - g.lx0 + F->xs;
        int cum = 0;
        for (int d = 0; d < 4; d++) {
            cum += F->colw[d];
            if (vrel >= cum - 4 && vrel <= cum + 4) { F->coldrag = d + 1; return; }
        }
        int rel = lx - g.lx0;
        int col;
        if (rel >= g.mod_x) col = 3;
        else if (rel >= g.type_x) col = 2;
        else if (rel >= g.size_x) col = 1;
        else col = 0;
        if (col == F->sort_col) F->sort_desc = !F->sort_desc;
        else { F->sort_col = col; F->sort_desc = 0; }
        F->need_refresh = 1;
        return;
    }

    if (ly >= list_top + g.list_h) return;
    int row = (ly - list_top) / ROW_H + F->scroll;
    if (row < 0 || row >= F->nrows) {
        sel_clear();
        F->band = 1;
        int bx = lx;
        if (bx < g.lx0) bx = g.lx0;
        if (bx > g.lx0 + g.lw) bx = g.lx0 + g.lw;
        int by = ly;
        if (by < list_top) by = list_top;
        if (by > list_top + g.list_h) by = list_top + g.list_h;
        F->band_x0 = F->band_cx = bx;
        F->band_y0 = F->band_cur = by;
        return;
    }
    Row *r = &F->rows[row];

    int dbl = api->dclick() && !strcmp(F->last_click_name, r->name);
    strlcpy(F->last_click_name, r->name, sizeof F->last_click_name);
    if (!strcmp(r->name, "..")) { open_row(row); return; }
    if (dbl) { open_row(row); return; }

    int ctrl = api->kbd_mods() & 2;
    F->click_collapse = 0;
    if (ctrl) sel_toggle(r->name);
    else if (sel_has(r->name)) {
        F->click_collapse = 1;
        strlcpy(F->collapse_name, r->name, sizeof F->collapse_name);
    } else sel_set_single(r->name);
    F->press_row = 1;
    F->press_lx = lx;
    F->press_ly = ly;
    F->drag_sent = 0;
}

static void fm_drop(int inst, int lx, int ly, const char *type, const char *data)
{
    F = &fms[inst];
    if (strcmp(type, "file")) return;
    if (F->need_refresh) refresh();
    F->fm_msg[0] = 0;

    if (lx < SIDEBAR) {
        int d = sidebar_drive_hit(lx, ly);
        if (d == 0) copy_list_to(data, 0, "", "/", 1);
        else if (d == 1 && usb_present()) copy_list_to(data, 1, "", "/", 1);
        else strlcpy(F->fm_msg, "drop on a drive or folder", sizeof F->fm_msg);
        return;
    }

    FmGeo g;
    fm_geo(F->cliW, F->cliH, &g);
    int list_top = TOP_H + HDR_H;
    if (ly >= list_top && ly < list_top + g.list_h &&
        lx >= g.lx0 && lx < g.lx0 + g.lw) {
        int row = (ly - list_top) / ROW_H + F->scroll;
        if (row >= 0 && row < F->nrows && F->rows[row].is_dir &&
            strcmp(F->rows[row].name, "..")) {
            if (F->cur_drive == 0) copy_list_to(data, 0, F->rows[row].name, "/", 1);
            else {
                char p[160];
                if (strlen(F->cur_path) > 1)
                    kfmt(p, sizeof p, "%s/%s", F->cur_path, F->rows[row].name);
                else kfmt(p, sizeof p, "/%s", F->rows[row].name);
                copy_list_to(data, 1, "", p, 1);
            }
            return;
        }
    }
    copy_list_to(data, F->cur_drive, F->a_dir, F->cur_path, 1);
}

static void files_csize(int inst, int *w, int *h) { (void)inst; fileman_client_size(w, h); }
static void files_minc(int *w, int *h) { fileman_min_client(w, h); }

static void folder_event(const char *event,const char *data)
{
    if(!strcmp(event,"file.changed")||!strcmp(event,"file.saved")){
        for(int i=0;i<MAXINST;i++)fms[i].need_refresh=1;return;
    }
    if(strcmp(event,"folder.open"))return;
    char path[FS_NAMELEN];if(strlen(data)>=sizeof path)return;strlcpy(path,data,sizeof path);
    int inst=win_open(files_type);if(inst<0)return;F=&fms[inst];nav_to(0,path,"/");
}
const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Files"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    fileman_init();
    static const AppDesc d = {
        .title = "Files", .max_inst = MAXINST, .resizable = 1, .in_menu = 1,
        .open = fileman_open, .draw = fileman_draw, .key = fileman_key,
        .mouse = fileman_mouse, .wheel = fileman_wheel,
        .client_size = files_csize,
        .min_client = files_minc, .drop = fm_drop,
    };
    files_type = api->register_app(&d);
    fp_register();
    api->on_event(folder_event);
    return files_type < 0;
}
