/* Manages windows and draws the desktop. */
#include "os.h"
#include "flipgate.inc"
#include "pumpbtn.inc"
#include "winfit.inc"
#include "winhit.inc"
#include "framegate.inc"
#include "hangwatch.inc"
#include "atsw.inc"
#include "bmpw.inc"
#include "shotname.inc"
#include "../kexts/gdi.h"
#include "../kexts/g3d.h"
#include "../kexts/menushade.h"

Win wins[MAXWIN];
static int zord[MAXWIN];
static int nz;
static u8 desktop_focus;

static int at_k = -1;

int mx, my;
u8 gui_dirty;
u8 gui_blink;
u8 gui_up;

static char fault_banner[72];
static u32  fault_banner_until;

static char busy_title[24], busy_msg[44];
static int  busy_frac = -1;
static u8   busy_on;
void fault_show_banner(const char *msg)
{
    strlcpy(fault_banner, msg, sizeof fault_banner);
    fault_banner_until = ticks + 500;
    gui_dirty = 1;
}

static u8 mbtn_prev;
static int drag_win = -1, drag_ox, drag_oy;
static int resize_win = -1, resize_ox, resize_oy;
static int press_win = -1;
static int press_x, press_y, control_window = -1, redraw_window = -1;
static int press_desk;
static int menu_open;

static void (*ov_draw)(void);
static int  (*ov_mouse)(int x, int y, int ev);
static int  (*ov_key)(int k);
static int  ov_owner = -1;

void set_overlay_key(int (*key)(int k)) { ov_key = key; }
int overlay_modal(void){return ov_mouse!=0;}

void set_overlay(void (*draw)(void), int (*mouse)(int x, int y, int ev))
{
    if(mouse){press_win=-1;press_desk=0;}
    ov_key = 0;
    ov_draw = draw;
    ov_mouse = mouse;
    ov_owner = (draw || mouse) ? kext_owner_now() : -1;
    gui_dirty = 1;
}

static void ov_drop(void)
{
    services_dialog_drop();
    ov_draw = 0; ov_mouse = 0; ov_key = 0; ov_owner = -1;
    gui_dirty = 1;
}

void overlay_drop_owner(int owner)
{
    if (owner < 0 || (ov_owner != owner && services_dialog_owner()!=owner)) return;
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i].used && app_type_owner(wins[i].type) == owner) return;
    ov_drop();
}

static u8   dnd_on;
static char dnd_type[16], dnd_data[4096], dnd_label[16];
int drag_active(void) { return dnd_on; }
int drag_start(const char *type, const char *data)
{
    if (!type || !data) return -1;
    strlcpy(dnd_type, type, sizeof dnd_type);
    strlcpy(dnd_data, data, sizeof dnd_data);
    int nlines = data[0] ? 1 : 0;
    const char *b = data;
    for (const char *p = data; *p; p++) {
        if (*p == '\n') nlines++;
        else if (*p == '/' || *p == ':') b = p + 1;
    }
    if (nlines > 1) kfmt(dnd_label, sizeof dnd_label, "%d files", nlines);
    else {
        int i = 0;
        while (b[i] && b[i] != '\n' && i < (int)sizeof dnd_label - 1)
            { dnd_label[i] = b[i]; i++; }
        dnd_label[i] = 0;
    }
    dnd_on = 1;
    gui_dirty = 1;
    return 0;
}
#define GRIP 14
static char clockstr[12];
static char datestr[12];

#define BORDER 3
#define TITLEH 18
#define CLIX(v) ((v)->x + BORDER)
#define CLIY(v) ((v)->y + BORDER + TITLEH + 1)
#define CLIW(v) ((v)->w - 2 * BORDER)
#define CLIH(v) ((v)->h - (2 * BORDER + TITLEH + 1))

#include "winimage.inc"
static struct {char title[24],msg[44];int on,frac;} progress[MAXWIN];
void app_local_progress(const char *title,const char *msg,int frac)
{
    int i=app_current_window();if(i<0)return;
    progress[i].on=title!=0;progress[i].frac=frac;
    strlcpy(progress[i].title,title?title:"",sizeof progress[i].title);
    strlcpy(progress[i].msg,msg?msg:"",sizeof progress[i].msg);gui_dirty=1;
}

#define MENUW  136
#define MITEMH 20
#define MSEP   6

enum { CAT_PROG, CAT_GAME, CAT_SYS, CAT_DEV, CAT_ALL, NCAT };
static const char *const cat_name[NCAT] = {
    "Programs", "Games", "System", "Development", "All apps"
};
static int open_cat = -1;

static int app_cat(int type)
{
    const AppDesc *d = app_desc(type);

    if (d->category >= APP_CAT_PROGRAMS && d->category <= APP_CAT_DEV)
        return d->category - APP_CAT_PROGRAMS;
    static const char *const sys[] = {
        "System Info", "Memory Map", "Memory Editor",
        "Benchmark", "Settings", "About"
    };
    static const char *const game[] = { "Minesweeper", "Reversi", "Snake" };
    for (int i = 0; i < (int)(sizeof sys / sizeof sys[0]); i++)
        if (!strcmp(d->title, sys[i])) return CAT_SYS;
    for (int i = 0; i < (int)(sizeof game / sizeof game[0]); i++)
        if (!strcmp(d->title, game[i])) return CAT_GAME;
    return CAT_PROG;
}

static int cat_count(int cat)
{
    int n = 0;
    for (int t = 0; t < app_count(); t++) {
        const AppDesc *d = app_desc(t);
        if (d->in_menu && (cat == CAT_ALL || app_cat(t) == cat)) n++;
    }
    return n;
}

static int cat_app(int cat, int idx)
{
    int n = 0;
    for (int t = 0; t < app_count(); t++) {
        const AppDesc *d = app_desc(t);
        if (!d->in_menu || (cat != CAT_ALL && app_cat(t) != cat)) continue;
        if (n == idx) return t;
        n++;
    }
    return -1;
}

static void topmenu_geo(int *x0, int *y0, int *mh)
{
    *mh = NCAT * MITEMH + MSEP + MITEMH + 4;
    *x0 = 2;
    *y0 = SH - TBH - *mh;
}

static int topitem_y(int y0, int i)
{
    if (i < NCAT) return y0 + 2 + i * MITEMH;
    return y0 + 2 + NCAT * MITEMH + MSEP;
}

static void submenu_geo(int cat, int *sx, int *sy, int *sw, int *sh)
{
    int x0, y0, mh;
    topmenu_geo(&x0, &y0, &mh);
    int n = cat_count(cat), wide = 0;
    for (int i = 0; i < n; i++) {
        int l = (int)strlen(app_desc(cat_app(cat, i))->title);
        if (l > wide) wide = l;
    }
    *sw = wide * 8 + 20;
    if (*sw < 96) *sw = 96;
    *sh = n * MITEMH + 4;
    *sx = x0 + MENUW - 2;
    *sy = topitem_y(y0, cat) - 2;
    if (*sy + *sh > SH - TBH) *sy = SH - TBH - *sh;
    if (*sy < 0) *sy = 0;
}

static int topcat_at(int px, int py)
{
    int x0, y0, mh;
    topmenu_geo(&x0, &y0, &mh);
    if (px < x0 || px >= x0 + MENUW) return -1;
    for (int i = 0; i <= NCAT; i++) {
        int iy = topitem_y(y0, i);
        if (py >= iy && py < iy + MITEMH) return i;
    }
    return -1;
}

static int submenu_at(int px, int py)
{
    if (open_cat < 0) return -1;
    int sx, sy, sw, sh;
    submenu_geo(open_cat, &sx, &sy, &sw, &sh);
    if (px < sx || px >= sx + sw || py < sy || py >= sy + sh) return -1;
    int idx = (py - sy - 2) / MITEMH;
    return (idx >= 0 && idx < cat_count(open_cat)) ? idx : -1;
}

static int in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int focused(void) { return nz && !desktop_focus ? zord[nz - 1] : -1; }

static int tb_btnw(void)
{
    int n = 0;
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i].used) n++;
    if (!n) return 100;
    int w = (SW - 162 - 64) / n - 4;
    if (w > 100) w = 100;
    if (w < 28) w = 28;
    return w;
}

int win_is_focused(Win *w) { int f = focused(); return f >= 0 && &wins[f] == w; }

int win_is_hovered(Win *w)
{
    static WhRect r[MAXWIN];
    for (int i = 0; i < MAXWIN; i++) {
        r[i].x = wins[i].x; r[i].y = wins[i].y;
        r[i].w = wins[i].used ? wins[i].w : 0;
        r[i].h = wins[i].used ? wins[i].h : 0;
    }
    int top = wh_top_at(zord, nz, r, mx, my);
    return top >= 0 && &wins[top] == w;
}

int control_state(int x, int y, int w, int h)
{
    if (!in(mx,my,x,y,w,h)) return 0;
    if (control_window >= 0) {
        Win *v = &wins[control_window];
        if (ov_draw || menu_open || busy_on || !win_is_hovered(v) ||
            !in(mx,my,CLIX(v),CLIY(v),CLIW(v),CLIH(v))) return 0;
        if (press_win != control_window) return 1;
    } else if (control_window != -2) return 0;
    return 1 | ((mbtn_prev & 1) && in(press_x,press_y,x,y,w,h) ? 2 : 0);
}

static int win_find(int type,int inst);
void win_redraw(int type, int inst)
{
    int i = win_find(type,inst);
    if (i < 0) return;
    if (gui_dirty == 1 || (gui_dirty == 2 && redraw_window != i)) gui_dirty = 1;
    else { redraw_window = i; gui_dirty = 2; }
}

static void win_raise(int i)
{
    for (int k = 0; k < nz; k++)
        if (zord[k] == i) {
            if (desktop_focus || zord[nz - 1] != i) key_clear_held();
            desktop_focus = 0;
            for (; k < nz - 1; k++) zord[k] = zord[k + 1];
            zord[nz - 1] = i;
            return;
        }
}

static int win_find(int type, int inst)
{
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i].used && wins[i].type == type && wins[i].inst == inst)
            return i;
    return -1;
}

static const char *win_title(Win *w)
{
    return w->tbuf_on ? w->tbuf : w->title;
}

void win_set_title(int type, int inst, const char *title)
{
    int i = win_find(type, inst);
    if (i < 0) return;
    if (title && title[0]) {
        strlcpy(wins[i].tbuf, title, sizeof wins[i].tbuf);
        wins[i].tbuf_on = 1;
    } else wins[i].tbuf_on = 0;
    gui_dirty = 1;
}

void win_focus(int type, int inst)
{
    int i = win_find(type, inst);
    if (i >= 0) { win_raise(i); gui_dirty = 1; }
}

void win_close_self(int type, int inst)
{
    int i = win_find(type, inst);
    if (i >= 0) win_close(i);
}

const Win *win_slot(int i)
{
    return (i >= 0 && i < MAXWIN && wins[i].used) ? &wins[i] : 0;
}

int win_max(void) { return MAXWIN; }

static int anim_claims;
void anim_claim(int on)
{
    anim_claims += on ? 1 : -1;
    if (anim_claims < 0) anim_claims = 0;
}

int mouse_buttons(void) { return mbtn_prev & 3; }

static int dclick_flag;
int dclick(void) { return dclick_flag; }

static int drect_on, drect_x0, drect_y0;
void drag_rect_begin(void) { drect_on = 1; drect_x0 = mx; drect_y0 = my; }
int drag_rect_get(int *x0, int *y0, int *x1, int *y1)
{
    int a = drect_x0, b = drect_y0, c = mx, d = my;
    if (x0) *x0 = a < c ? a : c;
    if (y0) *y0 = b < d ? b : d;
    if (x1) *x1 = a < c ? c : a;
    if (y1) *y1 = b < d ? d : b;
    return drect_on && (mbtn_prev & 1);
}

void mouse_warp(int x, int y)
{
    mx = x; my = y;
    if (mx < 0) mx = 0; if (my < 0) my = 0;
    if (mx >= SW) mx = SW - 1; if (my >= SH) my = SH - 1;
    gui_dirty = 1;
}

static volatile u8 presenting;
static volatile int present_thr = -1;
int present_try(void)
{
    u32 f = irq_save();
    int ok = !presenting;
    if (ok) { presenting = 1; present_thr = thr_self; }
    irq_restore(f);
    return ok;
}
void present_done(void) { present_thr = -1; presenting = 0; }

void present(void)
{
    if (!present_try()) { gui_dirty = 1; return; }
    gui_compose();
    flip();
    present_done();
}

static u8 close_pending[MAXWIN];

void win_close(int i)
{

    if(i<0||i>=MAXWIN||!wins[i].used)return;
    if(app_handler_running(i)){close_pending[i]=1;app_cancel_window(i);if(app_unresponsive(i))app_kill_request(i);return;}
    close_pending[i]=0;app_forget_window(i);win_image_free(i);progress[i].on=0;
    for (int k = 0; k < nz; k++)
        if (zord[k] == i) {
            for (; k < nz - 1; k++) zord[k] = zord[k + 1];
            nz--;
            break;
        }
    app_free(wins[i].type, wins[i].inst);
    wins[i].used = 0;

    overlay_drop_owner(app_type_owner(wins[i].type));

    if (drag_win == i)   drag_win = -1;
    if (resize_win == i) resize_win = -1;
    if (press_win == i)  press_win = -1;
}

void win_close_flush(void)
{
    for(int i=0;i<MAXWIN;i++)if(close_pending[i]&&!app_handler_running(i)){close_pending[i]=0;win_close(i);gui_dirty=1;}
}

void win_fit_client(int type, int inst, int cw, int ch)
{
    for (int i = 0; i < MAXWIN; i++) {
        Win *w = &wins[i];
        if (!w->used || w->type != type || w->inst != inst) continue;

        WinFit f = win_fit(w->x, w->y, cw + 2 * BORDER,
                           ch + 2 * BORDER + TITLEH + 1, SW, SH - TBH);
        w->x = f.x; w->y = f.y; w->w = f.w; w->h = f.h;
        gui_dirty = 1;
        return;
    }
}

static const char *type_title(int t)
{
    const AppDesc *d = app_desc(t);
    return d ? d->title : "?";
}

int win_open(int type)
{
    if (app_multi(type) <= 1) {
        for (int i = 0; i < MAXWIN; i++)
            if (wins[i].used && wins[i].type == type) {
                win_raise(i);
                gui_dirty = 1;
                return wins[i].inst;
            }
    }

    int inst = app_alloc(type);
    if (inst < 0) {
        for (int k = nz - 1; k >= 0; k--)
            if (wins[zord[k]].type == type) {
                win_raise(zord[k]);
                gui_dirty = 1;
                return wins[zord[k]].inst;
            }
        return -1;
    }

    int i;
    for (i = 0; i < MAXWIN; i++)
        if (!wins[i].used) break;
    if (i == MAXWIN || nz == MAXWIN) { app_free(type, inst); return -1; }

    int cw, ch;
    app_client_size(type, inst, &cw, &ch);
    Win *w = &wins[i];
    w->used = 1;
    w->type = type;
    w->inst = inst;
    static int cascade;

    WinFit f = win_fit(24 + cascade * 24, 16 + cascade * 22,
                       cw + 2 * BORDER, ch + 2 * BORDER + TITLEH + 1,
                       SW, SH - TBH);
    cascade = (cascade + 1) % 6;
    w->x = f.x; w->y = f.y; w->w = f.w; w->h = f.h;
    w->title = type_title(type);
    zord[nz++] = i;
    desktop_focus = 0;
    gui_dirty = 1;
    {
        char tm[48];
        kfmt(tm, sizeof tm, "open %s", type_title(type));
        ktrace(tm);
    }
    return inst;
}

static void update_clock(void)
{
    int h, m, s, D, M, Y;
    rtc_read(&h, &m, &s, &D, &M, &Y);
    kfmt(clockstr, sizeof clockstr, "%02d:%02d:%02d", h, m, s);
    kfmt(datestr, sizeof datestr, "%04d-%02d-%02d", Y, M, D);
}

void gui_init(void)
{

    update_clock();

    gui_dirty = 1;
    gui_up = 1;
}

static u32 last_input;
static int ss_active;
static int ssx, ssy, ssdx = 3, ssdy = 2;
static u8  ss_col = C_BGREEN;
#define SS_W 120
#define SS_H 140

static const GdiOps *ss_gd;
static const G3dOps *ss_g3;
static G3D *ss_ctx;
static u8 ss_bound;
static void draw_floppy(int x, int y, u8 col);

static G3Vtx ss_cv[8];
static const u16 ss_ce[24] = {
    0,1, 1,2, 2,3, 3,0,  4,5, 5,6, 6,7, 7,4,  0,4, 1,5, 2,6, 3,7
};

static void ss_draw(void)
{
    fill_rect(0, 0, SW, SH, C_BLACK);
    ss_gd = (const GdiOps *)service_get("gdi");
    ss_g3 = (const G3dOps *)service_get("g3d");
    if (!ss_bound) {
        ss_bound = 1;
        ss_gd = (const GdiOps *)service_get("gdi");
        ss_g3 = (const G3dOps *)service_get("g3d");
        if (ss_gd && ss_gd->abi >= 10 && ss_g3 && ss_g3->abi >= 2)
            ss_ctx = ss_g3->create();
        for (int i = 0; i < 8; i++) {

            int r = i & 3;
            ss_cv[i].x = (r == 1 || r == 2) ? FX(1) : -FX(1);
            ss_cv[i].y = (r >= 2) ? FX(1) : -FX(1);
            ss_cv[i].z = (i & 4) ? FX(1) : -FX(1);
        }
    }
    if (!ss_ctx || !ss_gd || !ss_g3) {
        draw_floppy(ssx, ssy, ss_col);
        draw_text(ssx + 6, ssy + 58, "FLOPNIX", ss_col);
        return;
    }

    for (int i = 0; i < 8; i++) ss_cv[i].diffuse = GIDX(ss_col);
    GMat pm, wm;
    GVec t = { 0, 0, -FX(4), 0 };
    GVec r = { (fx)(((ticks * 190u) & 0xFFFFu) * 360u),
               (fx)(((ticks * 121u) & 0xFFFFu) * 360u), 0, 0 };
    ss_g3->viewport(ss_ctx, ssx, ssy, SS_W, SS_W, FX(1), FX(16));
    ss_g3->matrix_mode(ss_ctx, G3M_PROJ);
    ss_g3->mat_persp(&pm, FX(50), FX_ONE, FX(1), FX(16));
    ss_g3->load(ss_ctx, &pm);
    ss_g3->matrix_mode(ss_ctx, G3M_WORLD);
    ss_g3->mat_trs(&wm, &t, &r, 0);
    ss_g3->load(ss_ctx, &wm);
    ss_g3->draw(ss_ctx, G3P_LINES, G3F_XYZ | G3F_DIFFUSE, ss_cv, 8, ss_ce, 24);
    draw_text(ssx + SS_W / 2 - 28, ssy + SS_W + 4, "FLOPNIX", ss_col);
}

static int input_dismiss(void)
{
    last_input = ticks;
    if (ss_active) { ss_active = 0; gui_dirty = 1; return 1; }
    return 0;
}

void gui_tick(void)
{

    if (at_k >= 0 && !(kbd_mods() & 8)) {
        int k = at_k;
        at_k = -1;
        if (nz > 0) {
            if (k >= nz) k = nz - 1;
            win_raise(zord[k]);
        }
        gui_dirty = 1;
    }
    static u32 last, lasta;
    if ((u32)(ticks - last) >= 50) {
        last = ticks;
        gui_blink ^= 1;
        update_clock();
        gui_dirty = 1;
    }
    if (!ss_active && CFG->ss_enable &&
        (u32)(ticks - last_input) > (u32)CFG->ss_secs * 100) {
        ss_active = 1;
        ssx = SW / 3; ssy = SH / 3;
    }
    if (ss_active) {
        if ((u32)(ticks - lasta) >= 3) {
            lasta = ticks;
            ssx += ssdx; ssy += ssdy;
            static const u8 cols[6] = { C_BGREEN, C_CYAN, C_YELLOW, C_MAGENTA, C_RED, C_BBLUE };
            static int ci;
            if (ssx <= 0) { ssx = 0; ssdx = -ssdx; ss_col = cols[++ci % 6]; }
            if (ssx >= SW - SS_W) { ssx = SW - SS_W; ssdx = -ssdx; ss_col = cols[++ci % 6]; }
            if (ssy <= 0) { ssy = 0; ssdy = -ssdy; ss_col = cols[++ci % 6]; }
            if (ssy >= SH - SS_H) { ssy = SH - SS_H; ssdy = -ssdy; ss_col = cols[++ci % 6]; }
            gui_dirty = 1;
        }
    } else if (apps_animating() || anim_claims) {
        if ((u32)(ticks - lasta) >= 3) { lasta = ticks; gui_dirty = 1; }
    }
}

static volatile int esc_latched;
void esc_arm(void)     { esc_latched = 0; }
int  esc_pending(void) { return app_current_window()>=0 ? app_cancel_pending() : esc_latched; }

static PumpBtn pump_btn;

static volatile u8  pump_inside;
static volatile int pump_thr = -1;

void worker_unwind(int preempt_snap)
{
    keyboard_unwind();
    debug_unwind();
    u32 f = irq_save();
    if (pump_thr == thr_self)    { pump_inside = 0; pump_thr = -1; }
    if (present_thr == thr_self) { presenting = 0; present_thr = -1; }
    irq_restore(f);
    preempt_restore(preempt_snap);
}

int gui_pump(void)
{
    if (!gui_up || pump_inside) return 0;
    pump_inside = 1;
    pump_thr = thr_self;
    emergency_heartbeat();
    app_note_pump();

    int resident = kext_current();

    pump_keyboard();
    int esc = kbd_cancel_pending();
    if (esc) esc_latched = 1;

    u32 pk;
    while (mouse_pop(&pk, 0)) {
        u8 b0 = pk, b1 = pk >> 8, b2 = pk >> 16;
        int dx = b1 - ((b0 & 0x10) ? 256 : 0);
        int dy = b2 - ((b0 & 0x20) ? 256 : 0);

        gui_mouse(dx, -dy, pb_pump(&pump_btn, b0 & 7), ticks);
        if ((u8)(pk >> 24)) gui_wheel(-(int)(i8)(pk >> 24));
    }

    gui_tick();

    static u32 pump_last;
    if (gui_dirty && flip_due(ticks, &pump_last, timer_alive) && present_try()) {
        preempt_disable();
        gui_compose();
        preempt_enable();
        flip();
        present_done();
    }
    kext_enter(resident);
    pump_thr = -1;
    pump_inside = 0;
    return app_current_window()>=0?app_cancel_pending():esc;
}

void gui_wheel(int dz)
{
    if (input_dismiss()) return;
    int f = focused();
    if (f >= 0) { app_wheel(&wins[f], dz); gui_dirty = 1; }
}

static int shot_on_usb;
static int shot_exists(const char *name, void *ctx)
{
    (void)ctx;
    if (shot_on_usb) { char p[16]; p[0] = '/'; strlcpy(p + 1, name, 15);
                       return fat_exists(p) != 0; }
    return fs_exists(name);
}
static void screenshot(void)
{
    static u8 pal[256 * 3];
    for (int i = 0; i < 256; i++)
        palette_rgb(i, &pal[i * 3], &pal[i * 3 + 1], &pal[i * 3 + 2]);
    u32 fsz = bmpw_head(iobuf, SW, SH, pal);
    if (fsz > IOBUF_SZ) return;
    for (int y = 0; y < SH; y++)
        memcpy(bmpw_row(iobuf, SW, SH, y), BACKBUF + y * SPITCH, SW);

    shot_on_usb = fat_mount();
    char name[16];
    if (!shot_next(shot_exists, 0, name, sizeof name)) {
        notify("no free SHOTnn.BMP name");
        return;
    }
    int rc;
    busy_set("Screenshot", name, -1);
    if (shot_on_usb) { char p[16]; p[0] = '/'; strlcpy(p + 1, name, 15);
                       rc = fat_write(p, iobuf, fsz); }
    else rc = fs_write(name, iobuf, fsz);
    busy_end();
    char m[48];
    if (rc == 0) kfmt(m, sizeof m, "saved %s to %s", name,
                      shot_on_usb ? "USB" : "A:");
    else         kfmt(m, sizeof m, "screenshot failed (%s full?)",
                      shot_on_usb ? "USB" : "A:");
    notify(m);
}

void gui_key(int k)
{
    if (input_dismiss()) return;
    if(k==27){esc_latched=1;int f=focused();if(f>=0&&!ov_mouse&&!menu_open)app_cancel_window(f);}
    if (k == K_PRTSC) { screenshot(); return; }
    if (key_hook_dispatch(k)) { gui_dirty = 1; return; }
    if (k == '\t' && (kbd_mods() & 8)) {
        if (nz >= 2) {
            if (at_k < 0 || at_k >= nz) at_k = nz - 1;
            at_k = at_next(nz, at_k, (kbd_mods() & 1) ? -1 : 1);
            gui_dirty = 1;
        }
        return;
    }
    if (at_k >= 0 && k == 27) { at_k = -1; gui_dirty = 1; return; }
    if (ov_mouse) {

        if (ov_key) {
            int (*ck)(int) = ov_key;
            volatile int eaten = 0;
            int resident = kext_current();
            kext_enter(ov_owner);
            FAULT_GUARD(eaten = ck(k), { eaten = 1; ov_drop(); });
            kext_enter(resident);
            if (eaten) { gui_dirty = 1; return; }
        }
        if (k == 27) ov_drop();
        return;
    }
    if(k==20&&(kbd_mods()&2)){
        menu_open=0;open_cat=-1;at_k=-1;
        win_open(WT_TERM);gui_dirty=1;return;
    }
    if (k == K_MENU) {
        menu_open = !menu_open;
        open_cat = -1;
        gui_dirty = 1;
        return;
    }
    if (menu_open && k == 27) { menu_open = 0; open_cat = -1; gui_dirty = 1; return; }
    int f = focused();
    if (f < 0) {

        if (desk_key(k)) gui_dirty = 1;
        return;
    }
    app_key(&wins[f], k);
    gui_dirty = 1;
}

static void dnd_finish(void)
{
    if (!dnd_on) return;
    dnd_on = 0;
    for (int k = nz - 1; k >= 0; k--) {
        int i = zord[k];
        Win *w = &wins[i];
        if (!in(mx, my, w->x, w->y, w->w, w->h)) continue;
        if (in(mx, my, CLIX(w), CLIY(w), CLIW(w), CLIH(w)))
            app_drop(w, mx - CLIX(w), my - CLIY(w), dnd_type, dnd_data);
        return;
    }
    if (my < SH - TBH) desk_drop(mx, my, dnd_type, dnd_data);
}

void gui_mouse(int dx, int dy, u8 btn, u32 when)
{

    pb_main(&pump_btn, btn);
    if (input_dismiss() && (dx || dy || btn)) return;
    int sp = CFG->mouse_speed ? CFG->mouse_speed : 2;
    mx += dx * sp / 2;
    my += dy * sp / 2;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= SW) mx = SW - 1;
    if (my >= SH) my = SH - 1;
    u8 was = mbtn_prev;
    mbtn_prev = btn;
    int lpress = (btn & 1) && !(was & 1);
    int rpress = (btn & 2) && !(was & 2);
    gui_dirty = 1;

    if (lpress) {
        press_x = mx; press_y = my;
        static u32 lpt; static int lpx, lpy;
        int dx = mx - lpx, dy = my - lpy;
        dclick_flag = timer_alive && (u32)(when - lpt) < 40 &&
                      dx * dx + dy * dy <= 25;
        lpt = when; lpx = mx; lpy = my;
    }

    if (menu_open) {
        int c = topcat_at(mx, my);
        if (c >= 0 && c < NCAT) open_cat = c;
        else if (c == NCAT) open_cat = -1;
    }

    if (ov_mouse) {
        int (*cur)(int, int, int) = ov_mouse;
        volatile int keep = 1, died = 0;
        int resident = kext_current();
        kext_enter(ov_owner);
        FAULT_GUARD({
            if (lpress)                    keep = cur(mx, my, EV_PRESS);
            else if (rpress)               keep = cur(mx, my, EV_RPRESS);
            else if ((btn & 1) && (was & 1))      cur(mx, my, EV_DRAG);
            else if (!(btn & 1) && (was & 1)) keep = cur(mx, my, EV_RELEASE);
        }, { died = 1; klog("overlay mouse handler faulted - overlay closed\n"); });
        kext_enter(resident);
        if (died) ov_drop();
        else if (keep!=1 && ov_mouse == cur)
            ov_drop();
        if (died || keep!=2) return;
    }

    if (drag_win >= 0) {
        Win *w = &wins[drag_win];
        w->x = mx - drag_ox;
        w->y = my - drag_oy;
        if (w->x < -(w->w - 40)) w->x = -(w->w - 40);
        if (w->x > SW - 40) w->x = SW - 40;
        if (w->y < 0) w->y = 0;
        if (w->y > SH - TBH - 20) w->y = SH - TBH - 20;
        if (!(btn & 1)) drag_win = -1;
        return;
    }

    if (resize_win >= 0) {
        Win *w = &wins[resize_win];
        int nw = mx + resize_ox - w->x;
        int nh = my + resize_oy - w->y;
        int minc_w, minc_h;
        app_min_client(w->type, &minc_w, &minc_h);
        int minw = minc_w + 2 * BORDER;
        int minh = minc_h + 2 * BORDER + TITLEH + 1;
        if (nw < minw) nw = minw;
        if (nh < minh) nh = minh;
        if (w->x + nw > SW) nw = SW - w->x;
        if (w->y + nh > SH - TBH) nh = SH - TBH - w->y;
        w->w = nw;
        w->h = nh;
        if (!(btn & 1)) resize_win = -1;
        return;
    }

    if (press_win >= 0) {
        Win *w = &wins[press_win];
        if (btn & 1)
            app_mouse(w, mx - CLIX(w), my - CLIY(w), EV_DRAG, CLIW(w), CLIH(w));
        if (!(btn & 1)) {
            app_mouse(w, mx - CLIX(w), my - CLIY(w), EV_RELEASE, CLIW(w), CLIH(w));
            press_win = -1;
            dnd_finish();
        }
        return;
    }

    if (press_desk) {
        if (btn & 1) desk_mouse(mx, my, EV_DRAG);
        else {
            desk_mouse(mx, my, EV_RELEASE);
            press_desk = 0;
            dnd_finish();
        }
        return;
    }

    if (rpress) {
        if (dnd_on) { dnd_on = 0; return; }
        if (menu_open) { menu_open = 0; open_cat = -1; return; }
        if (my >= SH - TBH) return;
        for (int k = nz - 1; k >= 0; k--) {
            int i = zord[k];
            Win *w = &wins[i];
            if (!in(mx, my, w->x, w->y, w->w, w->h)) continue;
            win_raise(i);
            if (in(mx, my, CLIX(w), CLIY(w), CLIW(w), CLIH(w)))
                app_mouse(w, mx - CLIX(w), my - CLIY(w), EV_RPRESS,
                          CLIW(w), CLIH(w));
            return;
        }
        desktop_focus = 1; key_clear_held();
        desk_mouse(mx, my, EV_RPRESS);
        return;
    }

    if (!lpress) return;

    if (menu_open) {
        int sub = submenu_at(mx, my);
        if (sub >= 0) {
            int t = cat_app(open_cat, sub);
            menu_open = 0; open_cat = -1;
            if (t >= 0) win_open(t);
            return;
        }
        int c = topcat_at(mx, my);
        if (c == NCAT) { menu_open = 0; open_cat = -1; reboot(); return; }
        if (c >= 0) { open_cat = c; return; }
        menu_open = 0; open_cat = -1;
        return;
    }

    if (my >= SH - TBH) {
        if (in(mx, my, 2, SH - TBH + 3, 56, 22)) { menu_open = 1; open_cat = -1; return; }
        int bw = tb_btnw();
        int bx = 64;
        for (int i = 0; i < MAXWIN; i++) {
            if (!wins[i].used) continue;
            if (bx + bw > SW - 162) break;
            if (in(mx, my, bx, SH - TBH + 3, bw, 22)) {
                if (focused() == i) {
                    for (int k = 0; k < nz; k++)
                        if (zord[k] == i) {
                            for (; k > 0; k--) zord[k] = zord[k - 1];
                            zord[0] = i;
                            break;
                        }
                } else win_raise(i);
                return;
            }
            bx += bw + 4;
        }
        return;
    }

    for (int k = nz - 1; k >= 0; k--) {
        int i = zord[k];
        Win *w = &wins[i];
        if (!in(mx, my, w->x, w->y, w->w, w->h)) continue;
        win_raise(i);
        int cbx = w->x + w->w - BORDER - 17, cby = w->y + BORDER + 1;
        if (in(mx, my, cbx, cby, 16, 16)) { win_close(i); return; }
        if (app_resizable(w->type) &&
            in(mx, my, w->x + w->w - GRIP, w->y + w->h - GRIP, GRIP, GRIP)) {
            resize_win = i;
            resize_ox = (w->x + w->w) - mx;
            resize_oy = (w->y + w->h) - my;
            return;
        }
        if (my < w->y + BORDER + TITLEH) {
            drag_win = i;
            drag_ox = mx - w->x;
            drag_oy = my - w->y;
            return;
        }
        if (in(mx, my, CLIX(w), CLIY(w), CLIW(w), CLIH(w))) {
            press_win = i;
            app_mouse(w, mx - CLIX(w), my - CLIY(w), EV_PRESS, CLIW(w), CLIH(w));
        }
        return;
    }

    desktop_focus = 1; key_clear_held();
    press_desk = 1;
    desk_mouse(mx, my, EV_PRESS);
}

static void draw_win(int i, int foc)
{
    Win *w = &wins[i];
    panel(w->x, w->y, w->w, w->h, 0);
    for (int j = 0; j < TITLEH; j++) {
        u8 c = foc ? C_TB0 + (j * 8) / TITLEH : C_G0 + 2;
        hline(w->x + BORDER, w->y + BORDER + j, w->w - 2 * BORDER, c);
    }
    const char *tt = win_title(w);
    char tbuf2[80];
    int waiting = app_busy(i);
    if (waiting && !app_unresponsive(i)) {
        kfmt(tbuf2, sizeof tbuf2, "%s - Working%s", tt, &"..."[2 - (ticks / 50) % 3]);
        tt = tbuf2;
    }
    if (app_unresponsive(i)) {
        kfmt(tbuf2, sizeof tbuf2, "%s (not responding)", tt);
        tt = tbuf2;
    }
    draw_text_clip(w->x + BORDER + 5, w->y + BORDER + 1, tt, C_WHITE, w->w - 30);
    int cbx = w->x + w->w - BORDER - 17, cby = w->y + BORDER + 1;
    int hov = in(mx, my, cbx, cby, 16, 16);
    panel(cbx, cby, 16, 16, hov);
    if (hov) fill_rect(cbx + 2, cby + 2, 12, 12, C_RED);
    draw_char(cbx + 4, cby, 'x', hov ? C_WHITE : C_BLACK);
    fill_rect(CLIX(w), CLIY(w), CLIW(w), CLIH(w), C_FACE);
    set_clip(CLIX(w), CLIY(w), CLIW(w), CLIH(w));

    if (app_handler_running(i) && !app_live_draw(w->type)) {
        if(!win_image_draw(i)){
        int bw = 150, bh = 30;
        int bx = CLIX(w) + (CLIW(w) - bw) / 2, by = CLIY(w) + (CLIH(w) - bh) / 2;
        if (CLIW(w) >= bw && CLIH(w) >= bh) {
            panel(bx, by, bw, bh, 0);
            char label[20];
            kfmt(label, sizeof label, "Working%s", &"..."[2 - (ticks / 50) % 3]);
            draw_text(bx + 12, by + 9, label, C_G0 + 3);
        }
        }
    } else {
        control_window = i;
        app_draw(w, CLIX(w), CLIY(w), CLIW(w), CLIH(w));
        control_window = -1;
        if(w->used&&!app_live_draw(w->type)&&!app_handler_running(i))win_image_save(i);
    }
    if(waiting&&CLIW(w)>100){
        int bx=CLIX(w)+4,by=CLIY(w)+CLIH(w)-22,bw=CLIW(w)-8;
        fill_rect(bx,by,bw,20,C_FACE);hline(bx,by,bw,C_SHAD);
        const char *msg=progress[i].on?(progress[i].msg[0]?progress[i].msg:progress[i].title):"Working...";
        draw_text_clip(bx+4,by+3,msg,C_NAVY,bw-12);
        if(progress[i].on&&progress[i].frac>=0){int f=progress[i].frac;if(f>256)f=256;hline(bx,by+19,bw*f/256,C_NAVY);}
    }
    clear_clip();
    if (app_resizable(w->type)) {
        int gx = w->x + w->w - BORDER - 2, gy = w->y + w->h - BORDER - 2;
        for (int a = 0; a < 3; a++)
            for (int b = 0; b <= a; b++) {
                int px = gx - b * 4, py = gy - (a - b) * 4;
                fill_rect(px - 1, py - 1, 2, 2, C_DARK);
                fill_rect(px, py, 1, 1, C_LIGHT);
            }
    }
}

static void draw_menu(void)
{
    int x0, y0, mh;
    topmenu_geo(&x0, &y0, &mh);
    panel(x0, y0, MENUW, mh, 0);
    menu_shade(&kapi,x0+2,y0+2,MENUW-4,mh-4,0);
    fill_rect(x0 + 2, y0 + 2, 22, mh - 4, C_TB0 + 4);
    for (int i = 0; i < NCAT; i++) {
        int iy = topitem_y(y0, i);
        int hov = (open_cat == i) || in(mx, my, x0 + 2, iy, MENUW - 4, MITEMH);
        if (hov) menu_shade(&kapi,x0+2,iy,MENUW-4,MITEMH,1);
        draw_text(x0 + 30, iy + 2, cat_name[i], hov ? C_WHITE : C_BLACK);
        draw_char(x0 + MENUW - 14, iy + 2, (char)0x10,
                  hov ? C_WHITE : C_G0 + 4);
    }
    int sepy = y0 + 2 + NCAT * MITEMH + MSEP / 2;
    hline(x0 + 6, sepy, MENUW - 12, C_SHAD);
    hline(x0 + 6, sepy + 1, MENUW - 12, C_LIGHT);
    int ry = topitem_y(y0, NCAT);
    int rhov = in(mx, my, x0 + 2, ry, MENUW - 4, MITEMH);
    if (rhov) menu_shade(&kapi,x0+2,ry,MENUW-4,MITEMH,1);
    draw_text(x0 + 30, ry + 2, "Reboot", rhov ? C_WHITE : C_BLACK);

    if (open_cat >= 0) {
        int sx, sy, sw, sh, n = cat_count(open_cat);
        submenu_geo(open_cat, &sx, &sy, &sw, &sh);
        panel(sx, sy, sw, sh, 0);
        menu_shade(&kapi,sx+2,sy+2,sw-4,sh-4,0);
        for (int i = 0; i < n; i++) {
            int iy = sy + 2 + i * MITEMH;
            int hov = in(mx, my, sx + 2, iy, sw - 4, MITEMH);
            if (hov) menu_shade(&kapi,sx+2,iy,sw-4,MITEMH,1);
            draw_text(sx + 8, iy + 2, app_desc(cat_app(open_cat, i))->title,
                      hov ? C_WHITE : C_BLACK);
        }
    }
}

static void draw_floppy(int x, int y, u8 col)
{
    fill_rect(x, y, 60, 54, col);
    fill_rect(x + 12, y, 30, 20, C_G0 + 2);
    fill_rect(x + 30, y + 2, 8, 15, col);
    fill_rect(x + 8, y + 26, 44, 24, C_WHITE);
    for (int i = 0; i < 3; i++)
        hline(x + 12, y + 32 + i * 5, 36, C_G0 + 3);
    bevel(x, y, 60, 54, 0);
}

void gui_frame_state(FrameState *s)
{
    s->dirty = gui_dirty;
    s->mx = mx;
    s->my = my;
    s->blink = gui_blink;
    s->ss = ss_active;
}

void gui_compose(void)
{
    int partial = gui_dirty == 2 && redraw_window >= 0 && nz > 0 &&
        zord[nz-1] == redraw_window && wins[redraw_window].x>=0 && wins[redraw_window].y>=0 &&
        wins[redraw_window].x+wins[redraw_window].w<=SW && wins[redraw_window].y+wins[redraw_window].h<=SH-TBH && !ss_active && !ov_draw && !menu_open &&
        !busy_on && !dnd_on && !drect_on && at_k < 0 &&
        !(fault_banner[0] && ticks < fault_banner_until);
    gui_dirty = 0;
    clear_clip();
    if (partial) {
        draw_win(redraw_window,focused()==redraw_window);
        debug_draw();
        draw_cursor(mx,my);
        return;
    }
    if (ss_active) {
        FAULT_GUARD(ss_draw(), { ss_active = 0; });
        if (ss_active) {debug_draw();return;}
    }
    fill_rect(0, 0, SW, SH, C_DESK);
    draw_text(8, 6, OS_NAME " " OS_VER, C_G0 + 6);
    desk_draw();

    for (int k = 0; k < nz; k++)
        draw_win(zord[k], k == nz - 1);

    fill_rect(0, SH - TBH, SW, TBH, C_FACE);
    hline(0, SH - TBH, SW, C_LIGHT);
    int mhov = menu_open || in(mx, my, 2, SH - TBH + 3, 56, 22);
    panel(2, SH - TBH + 3, 56, 22, menu_open);
    if (mhov && !menu_open) fill_rect(4, SH - TBH + 5, 52, 18, C_HILITE);

    draw_text(2 + (56 - text_width("Menu")) / 2, SH - TBH + 7, "Menu",
              (mhov && !menu_open) ? C_WHITE : C_BLACK);
    int bw = tb_btnw();
    int bx = 64;
    for (int i = 0; i < MAXWIN; i++) {
        if (!wins[i].used) continue;
        if (bx + bw > SW - 162) break;
        int foc = focused() == i;
        int hov = in(mx, my, bx, SH - TBH + 3, bw, 22);
        panel(bx, SH - TBH + 3, bw, 22, foc);
        if (hov && !foc) fill_rect(bx + 2, SH - TBH + 5, bw - 4, 18, C_HILITE);
        draw_text_clip(bx + 8, SH - TBH + 7, win_title(&wins[i]),
                       (hov && !foc) ? C_WHITE : C_BLACK, bw - 12);
        bx += bw + 4;
    }
    panel(SW - 158, SH - TBH + 3, 156, 22, 1);
    draw_text(SW - 152, SH - TBH + 7, datestr, C_BLACK);
    draw_text(SW - 66, SH - TBH + 7, clockstr, C_BLACK);

    if (menu_open) draw_menu();
    if (ov_draw) {
        void (*cur)(void) = ov_draw;
        int resident = kext_current();
        kext_enter(ov_owner);
        control_window = -2;
        FAULT_GUARD(cur(), {
            klog("overlay draw handler faulted - overlay closed\n");
            ov_drop();
        });
        kext_enter(resident);
        control_window = -1;
    }

    if (drect_on && (mbtn_prev & 1)) {
        int x0, y0, x1, y1;
        drag_rect_get(&x0, &y0, &x1, &y1);
        for (int x = x0; x < x1; x += 2) { pixel(x, y0, C_WHITE); pixel(x, y1, C_WHITE); }
        for (int y = y0; y < y1; y += 2) { pixel(x0, y, C_WHITE); pixel(x1, y, C_WHITE); }
    }

    if (dnd_on) {
        int gx = mx + 12, gy = my + 8;
        if (gx + 110 > SW) gx = SW - 110;
        if (gy + 20 > SH - TBH) gy = SH - TBH - 20;
        panel(gx, gy, 110, 20, 0);
        fill_rect(gx + 4, gy + 4, 9, 12, C_WHITE);
        hline(gx + 5, gy + 7, 6, C_G0 + 3);
        hline(gx + 5, gy + 10, 6, C_G0 + 3);
        draw_text_clip(gx + 17, gy + 2, dnd_label, C_BLACK, 110 - 21);
    }
    if (fault_banner[0] && ticks < fault_banner_until) {
        int bw = (int)strlen(fault_banner) * 8 + 16;
        fill_rect(SW / 2 - bw / 2, 2, bw, 16, C_MAROON);
        rect(SW / 2 - bw / 2, 2, bw, 16, C_RED);
        draw_text(SW / 2 - bw / 2 + 8, 4, fault_banner, C_WHITE);
    }
    if (busy_on) {

        int w = 320, h = 96, x = (SW - w) / 2, y = (SH - h) / 2;
        panel(x, y, w, h, 0);
        draw_text(x + 16, y + 12, busy_title, C_NAVY);
        draw_text(x + 16, y + 34, busy_msg, C_BLACK);
        if (busy_frac >= 0) {
            panel(x + 16, y + 58, w - 32, 18, 1);
            int bw2 = (w - 36) * (busy_frac > 256 ? 256 : busy_frac) / 256;
            if (bw2 > 0) fill_rect(x + 18, y + 60, bw2, 14, C_NAVY);
        }
    }
    if (at_k >= 0 && nz > 0) {
        int k = at_k < nz ? at_k : nz - 1;
        int w = 228, rh = 16, h = nz * rh + 26;
        int x = (SW - w) / 2, y = (SH - TBH - h) / 2;
        panel(x, y, w, h, 0);
        draw_text(x + 8, y + 4, "Switch to", C_NAVY);
        for (int p = nz - 1, row = 0; p >= 0; p--, row++) {
            int ry = y + 20 + row * rh;
            if (p == k) fill_rect(x + 4, ry, w - 8, rh, C_HILITE);
            draw_text_clip(x + 10, ry + 2, win_title(&wins[zord[p]]),
                           p == k ? C_WHITE : C_BLACK, w - 20);
        }
    }
    debug_draw();
    draw_cursor(mx, my);
}

void busy_set(const char *title, const char *msg, int frac256)
{
    if(app_current_window()>=0){app_local_progress(title,msg,frac256);gui_pump();return;}
    strlcpy(busy_title, title, sizeof busy_title);
    strlcpy(busy_msg, msg, sizeof busy_msg);
    busy_frac = frac256;
    busy_on = 1;
    gui_dirty = 1;
    gui_pump();
}

void busy_end(void)
{
    if(app_current_window()>=0){app_local_progress(0,0,-1);return;}
    busy_on = 0;
    gui_dirty = 1;
}
