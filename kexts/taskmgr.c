#include "kapi.h"
#include "gdi.h"
#include "shpath.h"
#include "sbdrag.inc"
#include "modsort.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;
static int view;
static int mod_sel=-1;
static int mscroll;
static SbDrag sbd;

static int lst_y, lst_h, lst_vis, lst_total;
static int sel_id = -1;
static int scroll;
static int running;
static int cx0, cy0, cw0, ch0;
static int newtask;
static char runpath[48];
static int  runlen;
static char status[96];
static u16  memkb[32];

#define MENU_H  20
#define MPAD    8
#define SUMH    34
#define HDRH    18
#define ROWH    16
#define BTNH    22
#define RUNH    22
#define MINW    260
#define COL_PID  6
#define W_PID   34
#define W_MEM   64
#define W_CPU   52

static const char *const MENUS[3] = { "File", "View", "Help" };
enum { MA_NEW = 1, MA_END, MA_CLOSE, MA_TASKS, MA_CPU, MA_MODS, MA_ABOUT };
static u8 menu_codes[7];

static int menu_w(int i) { return (int)api->strlen(MENUS[i]) * 8 + MPAD * 2; }
static int menu_x(int i) { int x = 4; for (int k = 0; k < i; k++) x += menu_w(k); return x; }

static const char *wtitle(const Win *w) { return w->tbuf_on ? w->tbuf : w->title; }

static void pctstr(char *b, int cap, u32 pm) { api->kfmt(b, cap, "%u.%u%%", pm / 10, pm % 10); }

static const char *basename_of(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
    return b;
}

static u16 app_mem_kb(int type)
{
    if (type < 0 || type >= (int)(sizeof memkb / sizeof memkb[0])) return 0;
    if (memkb[type]) return memkb[type];
    const AppDesc *d = api->app_desc(type);
    if (!d || !d->title) return 0;
    int kc = api->kext_count();
    for (int i = 0; i < kc; i++) {
        const KextInfo *ki = api->kext_get(i);
        if (ki && ki->kind && !api->strcmp(ki->hname, d->title)) {
            memkb[type] = (u16)((ki->size + 1023) / 1024);
            return memkb[type];
        }
    }
    memkb[type] = 1;
    return 1;
}

static int row_count(void)
{
    int n = api->win_max(), c = 0;
    for (int i = 0; i < n; i++) {
        const Win *s = api->win_slot(i);
        if (s && s->used) c++;
    }
    return c;
}

static const Win *row_at(int idx, int *id)
{
    int n = api->win_max(), c = 0;
    for (int i = 0; i < n; i++) {
        const Win *s = api->win_slot(i);
        if (!s || !s->used) continue;
        if (c++ == idx) { if (id) *id = i; return s; }
    }
    return 0;
}

static void tm_tick(void *ctx) { (void)ctx; api->gui_dirty(); }

static void run_task(void)
{
    if (!runlen) { api->strlcpy(status, "type a path first", sizeof status); return; }
    char path[48];
    sh_norm_path(runpath, path, sizeof path);
    int L = (int)api->strlen(path);
    const char *base = basename_of(path);

    if (L > 3 && path[L - 3] == '.' && path[L - 2] == 'k' && path[L - 1] == 'x') {
        int drive; char rel[48]; sh_spec_split(runpath,&drive,rel,sizeof rel);
        sh_norm_path(rel,path,sizeof path); api->strlcpy(rel,path,sizeof rel);
        if (drive) { api->strlcpy(status,"Copy the module to the floppy first",sizeof status); goto done; }
        int rc=api->kext_load(rel);
        if (rc) { api->kfmt(status,sizeof status,"Could not load module (E%d)",rc); goto done; }
        api->strlcpy(status,api->open_with(rel,0,0,0)==0 ? "Module opened" : "Could not open its window",sizeof status);
    } else {
        int drive = 0; char rel[48];
        sh_spec_split(path, &drive, rel, sizeof rel);
        int n = drive ? api->fat_read(rel, api->iobuf, api->iobuf_size)
                      : api->fs_read(path, api->iobuf, api->iobuf_size);
        if (n < 0) api->strlcpy(status, "file not found", sizeof status);
        else if (api->open_with(base, path, api->iobuf, n) != 0)
            api->strlcpy(status, "no app opens that file", sizeof status);
        else api->strlcpy(status, "opened", sizeof status);
    }
done:
    runlen = 0; runpath[0] = 0; newtask = 0;
}

static void end_task(void)
{
    if (sel_id < 0) { api->strlcpy(status, "select a task first", sizeof status); return; }
    const Win *s = api->win_slot(sel_id);
    if (!s || !s->used) { sel_id = -1; return; }
    if (s->type == my_type) { api->strlcpy(status, "cannot end Task Manager", sizeof status); return; }
    api->win_close(sel_id);
    sel_id = -1;
    api->strlcpy(status, "task ended", sizeof status);
}

static void menu_pick(int idx, void *ctx)
{
    (void)ctx;
    if (idx < 0 || idx >= (int)sizeof menu_codes) return;
    switch (menu_codes[idx]) {
    case MA_NEW:   view = 0; sbd.active = 0; newtask = 1; runlen = 0; runpath[0] = 0; status[0] = 0; break;
    case MA_END:   end_task(); break;
    case MA_CLOSE: api->win_close_self(my_type, 0); return;
    case MA_TASKS: view = 0; sbd.active = 0; break;
    case MA_CPU:   view = 1; sbd.active = 0; break;
    case MA_MODS:  view = 2; sbd.active = 0; break;

    case MA_ABOUT: api->kfmt(status, sizeof status, "Task Manager - FLOPNIX %s",
                             api->os_version); break;
    }
    api->gui_dirty();
}

static void open_menu(int i)
{
    const char *items[6];
    int n = 0;
    if (i == 0) {
        items[n] = "New Task...";  menu_codes[n++] = MA_NEW;
        items[n] = "End Task";     menu_codes[n++] = MA_END;
        items[n] = "Close";        menu_codes[n++] = MA_CLOSE;
    } else if (i == 1) {
        items[n] = "Tasks";        menu_codes[n++] = MA_TASKS;
        items[n] = "CPU";          menu_codes[n++] = MA_CPU;
        items[n] = "Modules";      menu_codes[n++] = MA_MODS;
    } else {
        items[n] = "About";        menu_codes[n++] = MA_ABOUT;
    }
    api->menu_show(cx0 + menu_x(i), cy0 + MENU_H, items, n, menu_pick, 0);
}

static void gradient(int x, int y, int w, int h, GRGB a, GRGB b, int vert, u8 fb)
{
    if (w <= 0 || h <= 0) return;
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(x, y, w, h, a, b, vert);
        gfx->set_dither(0);
    } else api->fill_rect(x, y, w, h, fb);
}

static void meter(int x, int y, int w, int h, const char *label, u32 pm)
{
    if (w < 40) return;
    api->panel(x, y, w, h, 1);
    int inner = w - 4;
    int fill = (int)((u32)inner * (pm > 1000 ? 1000 : pm) / 1000);
    if (fill > 0)
        gradient(x + 2, y + 2, fill, h - 4, GRGB(120, 200, 255), GRGB(30, 90, 200), 0, C_BBLUE);
    char b[16];
    pctstr(b, sizeof b, pm);
    api->draw_text_clip(x + 6, y + (h - 12) / 2, label, C_BLACK, w - 60);
    api->draw_text(x + w - 6 - (int)api->strlen(b) * 8, y + (h - 12) / 2, b, C_BLACK);
}

static void draw_menubar(int cx, int cy, int cw)
{
    gradient(cx, cy, cw, MENU_H, GRGB(222, 226, 236), GRGB(190, 196, 212), 1, C_FACE);
    api->hline(cx, cy + MENU_H - 1, cw, C_SHAD);
    for (int i = 0; i < 3; i++)
        api->draw_text(cx + menu_x(i) + MPAD, cy + 4, MENUS[i], C_BLACK);
}

static int col_name(void) { return COL_PID + W_PID; }
static int col_mem(int cw)  { return cw - SB_W - 6 - W_CPU - W_MEM; }
static int col_cpu(int cw)  { return cw - SB_W - 6 - W_CPU; }

static void draw_tasks(int cx, int cy, int cw, int ch)
{
    int y = cy + MENU_H;

    u32 idle = api->cpu_usage(-1);
    u32 busy = idle < 1000 ? 1000 - idle : 0;
    u32 total_kb = (u32)api->boot_info(BI_MEM_KB);
    u32 used_kb  = api->mem_used_kb();

    u32 mempm    = total_kb ? used_kb * 1000 / total_kb : 0;
    int half = (cw - 18) / 2;
    meter(cx + 6, y + 6, half, 22, "Apps CPU", busy);
    char mb[32];
    api->kfmt(mb, sizeof mb, "Mem %uK", used_kb);
    meter(cx + 12 + half, y + 6, half, 22, mb, mempm);
    y += SUMH;

    gradient(cx + 4, y, cw - 8, HDRH, GRGB(232, 234, 240), GRGB(202, 206, 218), 1, C_FACE);
    api->panel(cx + 4, y, cw - 8, HDRH, 0);
    api->draw_text(cx + COL_PID + 4, y + 3, "PID", C_NAVY);
    api->draw_text(cx + col_name() + 4, y + 3, "Task", C_NAVY);
    api->draw_text(cx + col_mem(cw), y + 3, "Mem", C_NAVY);
    api->draw_text(cx + col_cpu(cw), y + 3, "CPU", C_NAVY);
    api->vline(cx + col_name(), y, HDRH, C_SHAD);
    api->vline(cx + col_mem(cw) - 6, y, HDRH, C_SHAD);
    api->vline(cx + col_cpu(cw) - 6, y, HDRH, C_SHAD);
    y += HDRH;

    int botrows = BTNH + 6 + (newtask ? RUNH : 0);
    int listh = ch - (y - cy) - botrows - 4;
    if (listh < ROWH) listh = ROWH;
    int vis = listh / ROWH;
    api->panel(cx + 4, y, cw - 8, listh, 1);
    running = row_count();
    if (scroll > running - vis) scroll = running - vis;
    if (scroll < 0) scroll = 0;

    for (int r = 0; r < vis; r++) {
        int id, idx = scroll + r;
        const Win *s = row_at(idx, &id);
        if (!s) break;
        int ry = y + 2 + r * ROWH;
        int sel = (id == sel_id);
        if (sel) api->fill_rect(cx + 6, ry, cw - 12 - SB_W, ROWH, C_HILITE);
        u8 fg = sel ? C_WHITE : C_BLACK;
        char b[16];
        api->kfmt(b, sizeof b, "%d", id);
        api->draw_text(cx + COL_PID + 4, ry + 2, b, sel ? C_WHITE : C_GRAY);
        api->draw_text_clip(cx + col_name() + 4, ry + 2, wtitle(s), fg,
                            col_mem(cw) - col_name() - 12);
        api->kfmt(b, sizeof b, "%uK", app_mem_kb(s->type));
        api->draw_text(cx + col_mem(cw), ry + 2, b, fg);
        pctstr(b, sizeof b, api->cpu_usage(s->type));
        api->draw_text(cx + col_cpu(cw), ry + 2, b, fg);
    }
    if (running > vis)
        api->draw_sbar(cx + cw - 6 - SB_W, y + 2, listh - 4, 0, running, vis, scroll);
    lst_y = y + 2 - cy; lst_h = listh - 4; lst_vis = vis; lst_total = running;
    y += listh + 4;

    if (newtask) {
        api->draw_text(cx + 6, y + 5, "Run:", C_NAVY);
        int fx = cx + 44, fw = cw - 44 - 62;
        api->panel(fx, y, fw, 18, 1);
        char shown[64];
        api->kfmt(shown, sizeof shown, "%s_", runpath);
        api->draw_text_clip(fx + 4, y + 3, shown, C_BLACK, fw - 8);
        api->panel(cx + cw - 56, y, 50, 18, 0);
        api->draw_text(cx + cw - 40, y + 3, "Run", C_GREEN);
        y += RUNH;
    }

    api->panel(cx + 6, y, 78, 20, 0);
    api->draw_text(cx + 14, y + 4, "New Task", C_BLACK);
    api->panel(cx + 90, y, 78, 20, 0);
    api->draw_text(cx + 97, y + 4, "End Task", sel_id >= 0 ? C_MAROON : C_GRAY);
    if (status[0]) api->draw_text_clip(cx + 178, y + 4, status, C_NAVY, cw - 186);
}

#define MOD_MAX FS_NFILES
static int mod_order(int *idx)
{
    static u32 sz[MOD_MAX];
    int n = api->kext_count();
    if (n > MOD_MAX) n = MOD_MAX;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        const KextInfo *k = api->kext_get(i);
        sz[i] = k ? k->size : 0;
    }
    return mod_sort(sz, n, MOD_MAX, idx);
}

static void module_load_result(const char *path,void *ctx)
{
    (void)ctx;
    if (!path) return;
    int drive; char rel[64]; sh_spec_split(path,&drive,rel,sizeof rel);
    if (drive) { api->strlcpy(status,"Copy the module to the floppy first",sizeof status); return; }
    api->busy_set("Loading module",rel,-1);
    int rc=api->kext_load(rel);
    api->busy_end();
    if (rc) api->kfmt(status,sizeof status,"Could not load module (E%d)",rc);
    else {
        api->strlcpy(status,"Module loaded",sizeof status);
        for (int i=0;i<api->kext_count();i++) {
            const KextInfo *k=api->kext_get(i);
            if (k && !api->strcmp(k->name,rel)) mod_sel=i;
        }
    }
    api->gui_dirty();
}
static void module_action(int action)
{
    if (action==0) { api->file_picker("Load module from floppy","kx",0,module_load_result,0); return; }
    const KextInfo *k=api->kext_get(mod_sel);
    if (!k) { api->strlcpy(status,"Select a module first",sizeof status); return; }
    if (action==2) { char path[FS_NAMELEN]; api->strlcpy(path,k->name,sizeof path); module_load_result(path,0); return; }
    int rc=api->kext_unload(mod_sel);
    api->strlcpy(status,rc==0 ? "Unloaded; memory stays reserved until restart" :
        rc==-2 ? "Required by a service or background task" :
        rc==-3 ? "Close its windows and wait for work to finish" : "This module is already inactive",sizeof status);
    api->gui_dirty();
}
static void draw_modules(int cx,int cy,int cw,int ch)
{
    int idx[MOD_MAX], m=mod_order(idx), loaded=0;
    u32 reserved=0;
    for (int i=0;i<m;i++) {
        const KextInfo *k=api->kext_get(idx[i]);
        if (k) { reserved+=k->size; if (!k->status) loaded++; }
    }
    char b[96],sz[16]; api->human_size(reserved,sz,sizeof sz);
    api->kfmt(b,sizeof b,"%d loaded / %s reserved",loaded,sz);
    int y=cy+MENU_H;
    api->draw_text_clip(cx+8,y+8,b,C_NAVY,cw-16); y+=SUMH;
    int state_x=cx+cw-174,size_x=cx+cw-78;
    gradient(cx+4,y,cw-8,HDRH,GRGB(232,234,240),GRGB(190,202,224),1,C_FACE);
    api->draw_text(cx+10,y+2,"Module",C_NAVY);
    api->draw_text(state_x,y+2,"State",C_NAVY);
    api->draw_text(size_x,y+2,"Memory",C_NAVY); y+=HDRH;
    int listh=ch-(y-cy)-72; if (listh<ROWH+4) listh=ROWH+4;
    int vis=(listh-4)/ROWH;
    api->panel(cx+4,y,cw-8,listh,1);
    if (mscroll>m-vis) mscroll=m-vis;
    if (mscroll<0) mscroll=0;
    for (int r=0;r<vis && mscroll+r<m;r++) {
        int id=idx[mscroll+r]; const KextInfo *k=api->kext_get(id);
        int ry=y+2+r*ROWH,selected=id==mod_sel;
        if (selected) gradient(cx+6,ry,cw-12-SB_W,ROWH,GRGB(62,110,172),GRGB(26,54,110),0,C_NAVY);
        else api->fill_rect(cx+6,ry,cw-12-SB_W,ROWH,(r&1) ? C_G0+7 : C_WHITE);
        u8 col=selected ? C_WHITE : C_BLACK;
        api->draw_text_clip(cx+10,ry,k->hname,col,state_x-cx-16);
        const char *state=k->status==46 ? "Unloaded" : k->status==45 ? "Disabled" :
            k->status ? "Failed" : k->kind==KEXT_KIND_KERNEL ? "System" : "Loaded";
        api->draw_text_clip(state_x,ry,state,col,88);
        api->human_size(k->size,sz,sizeof sz);
        api->draw_text_clip(size_x,ry,sz,col,58);
    }
    if (!m) api->draw_text(cx+10,y+6,"No modules loaded",C_GRAY);
    if (m>vis) api->draw_sbar(cx+cw-6-SB_W,y+2,listh-4,0,m,vis,mscroll);
    lst_y=y+2-cy; lst_h=listh-4; lst_vis=vis; lst_total=m;
    y+=listh+4;
    const KextInfo *selected=api->kext_get(mod_sel);
    api->draw_text_clip(cx+8,y,selected ? selected->name : "Select a module to manage it",C_NAVY,cw-16);
    y+=18;
    const char *labels[]={"Load...","Unload","Reload"};
    for (int i=0;i<3;i++) { int x=cx+6+i*82; api->panel(x,y,76,22,0); api->draw_text(x+8,y+3,labels[i],C_BLACK); }
    api->draw_text_clip(cx+8,y+26,status[0] ? status : "System and busy modules are protected",C_BLACK,cw-16);
}

static void draw_cpu(int cx, int cy, int cw, int ch)
{
    int y = cy + MENU_H + 8;
    u32 idle = api->cpu_usage(-1);
    u32 busy = idle < 1000 ? 1000 - idle : 0;
    api->draw_text(cx + 8, y, "CPU used by apps", C_NAVY);
    y += 16;
    meter(cx + 8, y, cw - 16, 24, "", busy);
    y += 34;
    api->draw_text(cx + 8, y, "Per task", C_NAVY);
    y += 16;
    int n = api->win_max(), row = 0;
    for (int i = 0; i < n; i++) {
        const Win *s = api->win_slot(i);
        if (!s || !s->used) continue;
        int ry = y + row * 20;
        if (ry + 18 > cy + ch - 6) break;
        api->draw_text_clip(cx + 10, ry + 2, wtitle(s), C_BLACK, 110);
        meter(cx + 128, ry, cw - 138, 16, "", api->cpu_usage(s->type));
        row++;
    }
}

static void tm_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    cx0 = cx; cy0 = cy; cw0 = cw; ch0 = ch;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    draw_menubar(cx, cy, cw);
    if (view == 0)      draw_tasks(cx, cy, cw, ch);
    else if (view == 2) draw_modules(cx, cy, cw, ch);
    else                draw_cpu(cx, cy, cw, ch);
}

static void tm_key(int inst, int k)
{
    (void)inst;
    if (!newtask) {
        int *sc = (view == 2) ? &mscroll : &scroll;
        if (k == K_UP   && *sc > 0) { (*sc)--; api->gui_dirty(); }
        if (k == K_DOWN)            { (*sc)++; api->gui_dirty(); }
        return;
    }
    if (k == '\n' || k == '\r') { run_task(); api->gui_dirty(); return; }
    if (k == 27) { newtask = 0; runlen = 0; runpath[0] = 0; api->gui_dirty(); return; }
    if (k == '\b') { if (runlen > 0) runpath[--runlen] = 0; api->gui_dirty(); return; }
    if (k >= 32 && k < 127 && runlen < (int)sizeof runpath - 1) {
        runpath[runlen++] = (char)k;
        runpath[runlen] = 0;
        api->gui_dirty();
    }
}

static void tm_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst;
    int *sc = (view == 2) ? &mscroll : &scroll;

    if (ev == EV_DRAG) {
        if (sbd.active && lst_h > 0) {
            *sc = sb_move(&sbd, lst_h, lst_total, lst_vis, ly - lst_y);
            api->gui_dirty();
        }
        return;
    }
    if (ev == EV_RELEASE) { sbd.active = 0; return; }
    if (ev != EV_PRESS) return;
    sbd.active = 0;

    if (view != 1 && lst_h > 0 && lx >= cw - 6 - SB_W &&
        ly >= lst_y && ly < lst_y + lst_h && lst_total > lst_vis) {
        *sc = sb_press(&sbd, lst_h, lst_total, lst_vis, *sc, ly - lst_y);
        api->gui_dirty();
        return;
    }

    if (ly < MENU_H) {
        for (int i = 0; i < 3; i++)
            if (lx >= menu_x(i) && lx < menu_x(i) + menu_w(i)) { open_menu(i); return; }
        return;
    }
    if (view==2) {
        if (ly>=lst_y && ly<lst_y+lst_vis*ROWH && lx<cw-6-SB_W) {
            int idx[MOD_MAX],n=mod_order(idx),r=mscroll+(ly-lst_y)/ROWH;
            if (r>=0 && r<n) { mod_sel=idx[r]; status[0]=0; }
        } else if (ly>=lst_y+lst_h+24 && ly<lst_y+lst_h+46) {
            for (int i=0;i<3;i++) if (lx>=6+i*82 && lx<82+i*82) module_action(i);
        }
        api->gui_dirty(); return;
    }
    if (view != 0) return;

    int y = MENU_H + SUMH + HDRH;
    int botrows = BTNH + 6 + (newtask ? RUNH : 0);
    int listh = ch - y - botrows - 4;
    if (listh < ROWH) listh = ROWH;
    int vis = listh / ROWH;

    if (ly >= y && ly < y + listh) {
        if (lx >= cw - 6 - SB_W) {
            if (ly < y + listh / 2) scroll -= vis; else scroll += vis;
            if (scroll > running - vis) scroll = running - vis;
            if (scroll < 0) scroll = 0;
            api->gui_dirty();
            return;
        }
        int r = (ly - y - 2) / ROWH, id = -1;
        if (r >= 0 && r < vis && row_at(scroll + r, &id)) { sel_id = id; api->gui_dirty(); }
        return;
    }
    y += listh + 4;
    if (newtask) {
        if (lx >= cw - 56 && ly >= y && ly < y + 18) { run_task(); api->gui_dirty(); return; }
        y += RUNH;
    }
    if (ly >= y && ly < y + 20) {
        if (lx >= 6 && lx < 84) {
            newtask = 1; runlen = 0; runpath[0] = 0; status[0] = 0; api->gui_dirty();
        } else if (lx >= 90 && lx < 168) {
            end_task(); api->gui_dirty();
        }
    }
}

static void tm_open(int inst)
{
    (void)inst;
    view = 0; mod_sel=-1; sel_id = -1; scroll = 0; mscroll = 0; newtask = 0;
    sbd.active = 0;
    runlen = 0; runpath[0] = 0; status[0] = 0;
    for (unsigned i = 0; i < sizeof memkb / sizeof memkb[0]; i++) memkb[i] = 0;
    if (timer_id < 0) timer_id = api->timer_add(25, tm_tick, 0);
}

static void tm_close(int inst) {
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

static void tm_min(int *w,int *h) { *w=360; *h=240; }
static void tm_csize(int inst, int *w, int *h) { (void)inst; *w = 408; *h = 288; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Task Manager"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Task Manager", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = tm_open, .draw = tm_draw, .mouse = tm_mouse, .key = tm_key, .close = tm_close,
        .client_size = tm_csize, .min_client=tm_min, .category = APP_CAT_DEV,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
