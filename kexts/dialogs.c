/* Provides message boxes, file pickers, and progress windows. */
#include "kapi.h"
#include "fspath.inc"
#include "shpath.h"
#include "savepath.inc"

static const Kapi *api;

static int wrap_count(const char *s, int maxc)
{
    int lines = 0;
    while (*s) {
        int len = 0, brk = 0;
        while (s[len] && len < maxc) { if (s[len] == ' ') brk = len; len++; }
        if (s[len] && brk) len = brk;
        s += len; while (*s == ' ') s++;
        lines++;
        if (lines >= 6) break;
    }
    return lines ? lines : 1;
}
static void wrap_draw(int x, int y, const char *s, int maxc, u8 col)
{
    int line = 0;
    while (*s && line < 6) {
        int len = 0, brk = 0;
        while (s[len] && len < maxc) { if (s[len] == ' ') brk = len; len++; }
        if (s[len] && brk) len = brk;
        char b[80];
        int n = len < 79 ? len : 79;
        api->memcpy(b, s, n); b[n] = 0;
        api->draw_text(x, y + line * 16, b, col);
        s += len; while (*s == ' ') s++;
        line++;
    }
}

static char mb_title[40], mb_text[192];
static int  mb_kind, mb_nb, mb_res[3];
static const char *mb_lbl[3];
static void (*mb_cb)(int, void *);
static void *mb_ctx;

static void mb_setbtns(void)
{
    switch (mb_kind) {
    case MB_OKCANCEL: mb_nb = 2; mb_lbl[0] = "OK"; mb_res[0] = MBR_OK;
                      mb_lbl[1] = "Cancel"; mb_res[1] = MBR_CANCEL; break;
    case MB_YESNO:    mb_nb = 2; mb_lbl[0] = "Yes"; mb_res[0] = MBR_YES;
                      mb_lbl[1] = "No"; mb_res[1] = MBR_NO; break;
    case MB_YESNOCANCEL: mb_nb = 3; mb_lbl[0] = "Yes"; mb_res[0] = MBR_YES;
                      mb_lbl[1] = "No"; mb_res[1] = MBR_NO;
                      mb_lbl[2] = "Cancel"; mb_res[2] = MBR_CANCEL; break;
    default:          mb_nb = 1; mb_lbl[0] = "OK"; mb_res[0] = MBR_OK; break;
    }
}
static void mb_geo(int *x, int *y, int *w, int *h)
{
    int tl = wrap_count(mb_text, 36);
    *w = 300; *h = 44 + tl * 16 + 30;
    *x = (*api->screen_w - *w) / 2;
    *y = (*api->screen_h - *h) / 2;
}
static void mb_btnrect(int i, int bx[4])
{
    int x, y, w, h;
    mb_geo(&x, &y, &w, &h);
    int bw = 64, gap = 8, tot = mb_nb * bw + (mb_nb - 1) * gap;
    bx[0] = x + (w - tot) / 2 + i * (bw + gap);
    bx[1] = y + h - 26;
    bx[2] = bw; bx[3] = 20;
}
static void mb_draw(void)
{
    int x, y, w, h;
    mb_geo(&x, &y, &w, &h);
    api->panel(x, y, w, h, 0);
    api->fill_rect(x + 2, y + 2, w - 4, 18, C_TB0 + 4);
    api->draw_text(x + 8, y + 3, mb_title, C_WHITE);
    wrap_draw(x + 12, y + 26, mb_text, 36, C_BLACK);
    for (int i = 0; i < mb_nb; i++) {
        int b[4]; mb_btnrect(i, b);
        int hov = *api->mouse_x >= b[0] && *api->mouse_x < b[0] + b[2] &&
                  *api->mouse_y >= b[1] && *api->mouse_y < b[1] + b[3];
        api->panel(b[0], b[1], b[2], b[3], hov);
        api->draw_text(b[0] + (b[2] - (int)api->strlen(mb_lbl[i]) * 8) / 2,
                       b[1] + 3, mb_lbl[i], C_BLACK);
    }
}
static int mb_mouse(int px, int py, int ev)
{
    if (ev != EV_PRESS) return 1;
    for (int i = 0; i < mb_nb; i++) {
        int b[4]; mb_btnrect(i, b);
        if (px >= b[0] && px < b[0] + b[2] && py >= b[1] && py < b[1] + b[3]) {
            void (*cb)(int, void *) = mb_cb; void *cx = mb_ctx; int r = mb_res[i];
            api->set_overlay(0, 0);
            if (cb) cb(r, cx);
            return 1;
        }
    }
    return 1;
}
static void d_msgbox(const char *title, const char *text, int buttons,
                     void (*cb)(int, void *), void *ctx)
{
    api->strlcpy(mb_title, title ? title : "Message", sizeof mb_title);
    api->strlcpy(mb_text, text ? text : "", sizeof mb_text);
    mb_kind = buttons; mb_cb = cb; mb_ctx = ctx;
    mb_setbtns();
    api->set_overlay(mb_draw, mb_mouse);
    api->gui_dirty();
}

static char tn_msg[64];
static u32  tn_t;
static void tn_draw(void)
{
    if ((u32)(*api->ticks - tn_t) > 250) { api->set_overlay(0, 0); return; }
    int tw = (int)api->strlen(tn_msg) * 8, w = tw + 24;
    if (w < 100) w = 100;
    int x = (*api->screen_w - w) / 2, y = 6;
    api->panel(x, y, w, 22, 0);
    api->fill_rect(x + 2, y + 2, w - 4, 18, C_TB0 + 3);
    api->draw_text(x + (w - tw) / 2, y + 4, tn_msg, C_WHITE);
}
static void d_notify(const char *text)
{
    api->strlcpy(tn_msg, text ? text : "", sizeof tn_msg);
    tn_t = *api->ticks;
    api->set_overlay(tn_draw, 0);
    api->gui_dirty();
}

static char pg_title[40], pg_label[40];
static int  pg_pct;
static void pg_draw(void)
{
    int w = 300, h = 84;
    int x = (*api->screen_w - w) / 2, y = (*api->screen_h - h) / 2;
    api->panel(x, y, w, h, 0);
    api->fill_rect(x + 2, y + 2, w - 4, 18, C_TB0 + 4);
    api->draw_text(x + 8, y + 3, pg_title, C_WHITE);
    api->draw_text(x + 12, y + 28, pg_label, C_BLACK);
    api->panel(x + 12, y + 48, w - 24, 18, 1);
    int fw = (w - 28) * (pg_pct < 0 ? 0 : pg_pct > 100 ? 100 : pg_pct) / 100;
    api->fill_rect(x + 14, y + 50, fw, 14, C_TB0 + 5);
    char pc[8]; api->kfmt(pc, sizeof pc, "%d%%", pg_pct);
    api->draw_text(x + w / 2 - 12, y + 51, pc, C_WHITE);
}
static int pg_mouse(int px, int py, int ev) { (void)px; (void)py; (void)ev; return 1; }
static void d_progress_open(const char *title)
{
    api->strlcpy(pg_title, title ? title : "Working", sizeof pg_title);
    pg_label[0] = 0; pg_pct = 0;
    api->set_overlay(pg_draw, pg_mouse);
    api->present();
}
static void d_progress_set(int pct, const char *label)
{
    pg_pct = pct;
    if (label) api->strlcpy(pg_label, label, sizeof pg_label);
    api->present();
}
static void d_progress_close(void) { api->set_overlay(0, 0); api->gui_dirty(); }

#define PK_ROWS 9
static char pk_title[40], pk_ext[8], pk_path[128];
static int  pk_dirs, pk_drive, pk_scroll, pk_n;
static int  pk_save, pk_nlen;
static char pk_name[64], pk_error[40];
static char pk_sel[80];
static void (*pk_cb)(const char *, void *);
static void *pk_ctx;
static char pk_names[80][64];
static u8   pk_isdir[80];
static FatEnt fe_scratch[80];

static int pk_match(const char *nm)
{
    if (!pk_ext[0]) return 1;
    int L = (int)api->strlen(nm), E = (int)api->strlen(pk_ext);
    return L > E + 1 && nm[L - E - 1] == '.' && !api->strcasecmp(nm + L - E, pk_ext);
}
static void pk_add(const char *nm, int isdir)
{
    if (pk_n >= 80) return;
    api->strlcpy(pk_names[pk_n], nm, 64);
    pk_isdir[pk_n++] = (u8)isdir;
}
static void pk_build(void)
{
    pk_n = 0; pk_error[0] = 0;
    if (pk_drive == 0) {
        if(pk_path[0])pk_add("..",1);
        for(int i=0;i<FS_NFILES;i++){
            FsEnt *e=api->fs_slot(i);if(!e||!e->used)continue;
            char child[FS_NAMELEN];int kind=fs_child(e->name,pk_path,child,sizeof child);if(!kind)continue;
            int dir=kind==2||(e->attr&FS_ATTR_DIR),have=0;
            for(int j=0;j<pk_n;j++)if(!api->strcmp(pk_names[j],child)){have=1;break;}
            if(!have&&(dir||(!pk_dirs&&pk_match(child))))pk_add(child,dir);
        }
    } else {
        int at_root = pk_path[0] == '/' && !pk_path[1];
        if (!at_root) pk_add("..", 1);
        int n = api->fat_list(pk_path[0] ? pk_path : "/", fe_scratch, 78);
        for (int i = 0; i < n; i++) {
            if (fe_scratch[i].is_dir) pk_add(fe_scratch[i].name, 1);
            else if (!pk_dirs && pk_match(fe_scratch[i].name)) pk_add(fe_scratch[i].name, 0);
        }
    }
    if (pk_scroll > pk_n - 1) pk_scroll = 0;
}
static const char *pk_disp(int i)
{
    const char *nm = pk_names[i];
    if (pk_drive == 0 && pk_path[0]) {
        int pl = (int)api->strlen(pk_path);
        if (!api->strncmp(nm, pk_path, pl) && nm[pl] == '/') return nm + pl + 1;
    }
    return nm;
}
static void pk_geo(int *x, int *y, int *w, int *h)
{
    *w = 300; *h = 40 + PK_ROWS * 16 + 30 + (pk_save ? 44 : 0);
    *x = (*api->screen_w - *w) / 2;
    *y = (*api->screen_h - *h) / 2;
}
static void pk_spec(const char *nm, char *out, int cap)
{
    if (pk_drive == 0) api->kfmt(out, cap, "a:%s", nm);
    else if (pk_path[0] && pk_path[1]) api->kfmt(out, cap, "u:%s/%s", pk_path, nm);
    else api->kfmt(out, cap, "u:/%s", nm);
}
static void pk_choose(const char *spec)
{
    void (*cb)(const char *, void *) = pk_cb; void *cx = pk_ctx;
    api->set_overlay(0, 0);
    if (cb) cb(spec, cx);
}

static void pk_name_set(const char *s)
{
    api->strlcpy(pk_name, s ? s : "", sizeof pk_name);
    pk_nlen = (int)api->strlen(pk_name);
    pk_error[0] = 0;
}

static void pk_save_commit(void)
{
    if (!pk_nlen) return;
    char spec[132];
    int result = save_path(pk_drive, pk_path, pk_name, pk_ext, spec, sizeof spec);
    const char *error = 0;
    if (result == SAVE_PATH_LONG) error = "Path too long (A: 23 chars max)";
    else if (result) error = "Invalid file path";
    else if (spec[0] == 'a') {
        const char *name = spec + 2;
        int parent = 0;
        for(int i=0;name[i];i++)if(name[i]=='/')parent=i;
        if (parent) {
            int found = 0;
            for (int i = 0; i < FS_NFILES; i++) {
                const FsEnt *e = api->fs_slot(i);
                if (e && e->used && !api->strncmp(e->name, name, parent) &&
                    (e->name[parent] == '/' ||
                     (!e->name[parent] && (e->attr & FS_ATTR_DIR)))) { found = 1; break; }
            }
            if (!found) error = "Folder not found";
        }
    }
    if (error) {
        api->strlcpy(pk_error, error, sizeof pk_error);
        api->gui_dirty(); return;
    }
    pk_choose(spec);
}

static void pk_draw(void)
{
    int x, y, w, h;
    pk_geo(&x, &y, &w, &h);
    api->panel(x, y, w, h, 0);
    api->fill_rect(x + 2, y + 2, w - 4, 18, C_TB0 + 4);
    api->draw_text(x + 8, y + 3, pk_title, C_WHITE);

    api->panel(x + w - 92, y + 2, 42, 16, pk_drive == 0);
    api->draw_text(x + w - 86, y + 3, "A:", C_BLACK);
    api->panel(x + w - 48, y + 2, 42, 16, pk_drive == 1);
    api->draw_text(x + w - 42, y + 3, "USB", api->usb_present() ? C_BLACK : C_G0 + 4);

    int ly = y + 22;
    api->fill_rect(x + 4, ly, w - 8, PK_ROWS * 16, C_WHITE);
    for (int v = 0; v < PK_ROWS; v++) {
        int i = pk_scroll + v;
        if (i >= pk_n) break;
        int ry = ly + v * 16;
        int hov = *api->mouse_x >= x + 4 && *api->mouse_x < x + w - 4 &&
                  *api->mouse_y >= ry && *api->mouse_y < ry + 16;
        int sel = !api->strcmp(pk_names[i], pk_sel);
        if (sel) api->fill_rect(x + 4, ry, w - 8, 16, C_HILITE);
        else if (hov) api->fill_rect(x + 4, ry, w - 8, 16, C_G0 + 7);
        u8 fg = sel ? C_WHITE : (pk_isdir[i] ? C_NAVY : C_BLACK);
        api->draw_text_clip(x + 8, ry, pk_disp(i), fg, w - 30);
        if (pk_isdir[i]) api->draw_char(x + w - 16, ry, (char)0x10, fg);
    }
    if (!pk_n) api->draw_text(x + 8, ly + 4, "(nothing here)", C_G0 + 3);

    api->panel(x + w - 20, ly, 14, 14, 0);
    api->draw_char(x + w - 17, ly - 1, (char)0x1E, C_BLACK);
    api->panel(x + w - 20, ly + PK_ROWS * 16 - 14, 14, 14, 0);
    api->draw_char(x + w - 17, ly + PK_ROWS * 16 - 15, (char)0x1F, C_BLACK);

    if (pk_save) {
        int fy = ly + PK_ROWS * 16 + 2;
        api->draw_text(x + 8, fy + 2, "Name:", C_BLACK);
        api->panel(x + 52, fy, w - 64, 18, 1);
        api->fill_rect(x + 54, fy + 2, w - 68, 14, C_WHITE);
        char shown[72];
        api->kfmt(shown, sizeof shown, "%s_", pk_name);
        const char *visible = shown;
        while (*visible && api->text_width(visible) > w - 74) visible++;
        api->draw_text_clip(x + 57, fy + 2, visible, C_BLACK, w - 74);
        char location[144];
        api->kfmt(location, sizeof location, "Folder: %s%s",
                  pk_drive ? "USB:" : "A:/", pk_path);
        api->draw_text_clip(x + 8, fy + 22, pk_error[0] ? pk_error : location,
                           pk_error[0] ? C_MAROON : C_NAVY, w - 16);
    }

    int by = y + h - 26;
    const char *ok = pk_save ? "Save" : pk_dirs ? "Choose" : "Open";
    int ho = *api->mouse_x >= x + 12 && *api->mouse_x < x + 76 &&
             *api->mouse_y >= by && *api->mouse_y < by + 20;
    api->panel(x + 12, by, 64, 20, ho);
    api->draw_text(x + 12 + (64 - (int)api->strlen(ok) * 8) / 2, by + 3, ok, C_BLACK);
    int hc = *api->mouse_x >= x + w - 76 && *api->mouse_x < x + w - 12 &&
             *api->mouse_y >= by && *api->mouse_y < by + 20;
    api->panel(x + w - 76, by, 64, 20, hc);
    api->draw_text(x + w - 76 + (64 - 48) / 2, by + 3, "Cancel", C_BLACK);
}
static int pk_mouse(int px, int py, int ev)
{
    if (ev != EV_PRESS) return 1;
    int x, y, w, h;
    pk_geo(&x, &y, &w, &h);
    if (py >= y + 2 && py < y + 18) {
        if (px >= x + w - 92 && px < x + w - 50) { pk_drive = 0; pk_path[0] = 0; pk_scroll = 0; pk_sel[0] = 0; pk_build(); }
        else if (px >= x + w - 48 && px < x + w - 6 && api->usb_present()) {
            pk_drive = 1; api->strlcpy(pk_path, "/", sizeof pk_path); pk_scroll = 0; pk_sel[0] = 0; pk_build();
        }
        return 1;
    }
    int ly = y + 22, by = y + h - 26;
    if (px >= x + w - 20 && px < x + w - 6) {
        if (py >= ly && py < ly + 14) { if (pk_scroll) pk_scroll--; }
        else if (py >= ly + PK_ROWS * 16 - 14 && py < ly + PK_ROWS * 16) {
            if (pk_scroll < pk_n - PK_ROWS) pk_scroll++;
        }
        return 1;
    }
    if (py >= by && py < by + 20) {
        if (px >= x + 12 && px < x + 76) {
            if (pk_save) pk_save_commit();
            else if (pk_dirs) {
                char spec[132];
                if (pk_drive == 0) api->kfmt(spec, sizeof spec, "a:%s", pk_path);
                else api->kfmt(spec, sizeof spec, "u:%s", pk_path);
                pk_choose(spec);
            } else if (pk_sel[0]) {
                char spec[132];
                pk_spec(pk_sel, spec, sizeof spec);
                pk_choose(spec);
            }
        } else if (px >= x + w - 76 && px < x + w - 12) pk_choose(0);
        return 1;
    }
    if (py >= ly && py < ly + PK_ROWS * 16) {
        int i = pk_scroll + (py - ly) / 16;
        if (i < 0 || i >= pk_n) return 1;
        if (pk_isdir[i]) {
            if (!api->strcmp(pk_names[i], "..")) {
                if (pk_drive == 0) { int cut=0;for(int k=0;pk_path[k];k++)if(pk_path[k]=='/')cut=k;pk_path[cut]=0; }
                else {
                    int cut = 0;
                    for (int k = 0; pk_path[k]; k++) if (pk_path[k] == '/') cut = k;
                    pk_path[cut ? cut : 1] = 0;
                }
            } else if (pk_drive == 0) api->strlcpy(pk_path, pk_names[i], sizeof pk_path);
            else {
                char np[128];
                if (pk_path[1]) api->kfmt(np, sizeof np, "%s/%s", pk_path, pk_names[i]);
                else api->kfmt(np, sizeof np, "/%s", pk_names[i]);
                api->strlcpy(pk_path, np, sizeof pk_path);
            }
            pk_scroll = 0; pk_sel[0] = 0; pk_build();
        } else {
            api->strlcpy(pk_sel, pk_names[i], sizeof pk_sel);
            if (pk_save) pk_name_set(pk_disp(i));
        }
        return 1;
    }
    return 1;
}

static int pk_key(int k)
{
    if (!pk_save) return 0;
    if (k == '\n' || k == '\r') { pk_save_commit(); return 1; }
    if (k == '\b') { if (pk_nlen) pk_name[--pk_nlen] = 0; pk_error[0] = 0; api->gui_dirty(); return 1; }
    if (save_path_char(k) &&
        pk_nlen < (int)sizeof pk_name - 1) {
        pk_name[pk_nlen++] = (char)k;
        pk_name[pk_nlen] = 0; pk_error[0] = 0;
        api->gui_dirty();
        return 1;
    }
    return 0;
}

static void d_file_save(const char *title, const char *ext, const char *defname,
                        void (*cb)(const char *, void *), void *ctx)
{
    api->strlcpy(pk_title, title ? title : "Save As", sizeof pk_title);
    api->strlcpy(pk_ext, ext ? ext : "", sizeof pk_ext);
    pk_dirs = 0; pk_cb = cb; pk_ctx = ctx;
    pk_drive = 0; pk_path[0] = 0; pk_scroll = 0; pk_sel[0] = 0;
    pk_save = 1;
    pk_name_set(defname);
    pk_build();
    api->set_overlay(pk_draw, pk_mouse);
    api->set_overlay_key(pk_key);
    api->gui_dirty();
}

static void d_file_picker(const char *title, const char *ext, int dirs_only,
                          void (*cb)(const char *, void *), void *ctx)
{
    pk_save = 0;
    api->strlcpy(pk_title, title ? title : (dirs_only ? "Choose folder" : "Open file"),
                 sizeof pk_title);
    api->strlcpy(pk_ext, ext ? ext : "", sizeof pk_ext);
    pk_dirs = dirs_only; pk_cb = cb; pk_ctx = ctx;
    pk_drive = 0; pk_path[0] = 0; pk_scroll = 0; pk_sel[0] = 0;
    pk_build();
    api->set_overlay(pk_draw, pk_mouse);
    api->gui_dirty();
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "Dialogs"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    static const DialogOps dops = {
        .msgbox         = d_msgbox,
        .notify         = d_notify,
        .file_picker    = d_file_picker,
        .file_save      = d_file_save,
        .progress_open  = d_progress_open,
        .progress_set   = d_progress_set,
        .progress_close = d_progress_close,
    };
    k->register_dialogs(&dops);
    return 0;
}
