#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;

#define HDR  26
#define ROWH 16
#define WINW 460
#define WINH 250

#define LIST_TOP  (HDR + 20)
#define LIST_BOT  80

static int list_rows(int ch)
{
    int r = (ch - LIST_TOP - LIST_BOT) / ROWH;
    return r < 0 ? 0 : r;
}

static const char *vec_name(u32 v)
{
    static const char *n[20] = {
        "divide error", "debug", "NMI", "breakpoint",
        "overflow", "bound range", "invalid opcode", "no FPU",
        "double fault", "FPU segment", "bad TSS", "segment absent",
        "stack fault", "protection fault", "page fault", "reserved",
        "FPU error", "alignment", "machine check", "SIMD"
    };
    return v < 20 ? n[v] : "unknown";
}

static int fl_is_open(void)
{
    if (my_type < 0) return 0;
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == (u8)my_type) return 1;
    }
    return 0;
}

static void tick(void *ctx) { (void)ctx; if (fl_is_open()) api->gui_dirty(); }

static u32 sel_sequence;
static int sel_valid, top;

static int sel_row(int n)
{
    if (!sel_valid) return -1;
    for (int i = 0; i < n; i++) {
        const FaultRec *r = api->fault_get(i);
        if (r && r->sequence == sel_sequence) return i;
    }
    return -1;
}

static void fl_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, HDR, GRGB(222, 226, 236),
                           GRGB(190, 196, 202), 1);
        gfx->set_dither(0);
    }
    api->hline(cx, cy + HDR - 1, cw, C_SHAD);

    int n = api->fault_count();
    char b[128];
    if (n == 0) {
        api->draw_text(cx + 8, cy + 6, "Fault Log", C_NAVY);
        api->draw_text(cx + 12, cy + HDR + 20,
                       "Nothing has crashed this session.", C_GRAY);
        api->draw_text(cx + 12, cy + HDR + 40,
                       "Crash Test can produce entries.", C_GRAY);
        return;
    }
    api->kfmt(b, sizeof b, "Fault Log - %d entr%s", n, n == 1 ? "y" : "ies");
    api->draw_text(cx + 8, cy + 6, b, C_NAVY);

    int hy = cy + HDR + 2;
    api->draw_text(cx + 8,   hy, "When",  C_NAVY);
    api->draw_text(cx + 72,  hy, "Fault", C_NAVY);
    api->draw_text(cx + 230, hy, "Location", C_NAVY);
    api->hline(cx + 4, hy + 14, cw - 8, C_SHAD);

    int listy = cy + LIST_TOP;
    int rows = list_rows(ch);

    if(top>n-rows)top=n>rows?n-rows:0;
    int selrow = sel_row(n);
    if (sel_valid && selrow < 0) sel_valid = 0;
    for (int i = top; i < n && i < top+rows; i++) {
        const FaultRec *r = api->fault_get(i);
        if (!r) break;
        int ry = listy + (i-top) * ROWH;
        int on = (i == selrow);
        if (on) api->fill_rect(cx + 4, ry, cw - 8, ROWH, C_HILITE);
        u8 fg = on ? C_WHITE : C_BLACK;

        api->kfmt(b, sizeof b, "%us", r->tick / 100);
        api->draw_text(cx + 8, ry + 2, b, on ? C_WHITE : C_GRAY);
        if (r->vec == FAULT_VEC_HANG)
            api->kfmt(b, sizeof b, "hung - task ended");
        else
            api->kfmt(b, sizeof b, "P%u %s", r->vec, vec_name(r->vec));
        api->draw_text_clip(cx + 72, ry + 2, b, on ? C_WHITE : C_MAROON, 152);
        api->draw_text_clip(cx + 230, ry + 2,
                           r->vec==FAULT_VEC_HANG?r->owner:r->location, fg, cw-238);
    }

    const FaultRec *r = selrow >= 0 ? api->fault_get(selrow) : 0;
    if (r) {
        if (r->vec == FAULT_VEC_HANG)
            api->kfmt(b, sizeof b, "stopped responding, ended by user  in %s",
                      r->owner);
        else {
            int chars=(cw-16)/8;
            if(chars<1)chars=1;
            if(chars>95)chars=95;
            api->strlcpy(b,r->location,chars+1);
            api->draw_text_clip(cx+8,cy+ch-72,b,C_NAVY,cw-16);
            if((int)api->strlen(r->location)>chars)
                api->draw_text_clip(cx+8,cy+ch-56,r->location+chars,C_NAVY,cw-16);
            api->kfmt(b,sizeof b,"Instruction address: %08x  P%u",r->eip,r->vec);
            api->draw_text_clip(cx+8,cy+ch-36,b,C_NAVY,cw-16);
            if(r->vec==14)api->kfmt(b,sizeof b,"Memory address: %08x  Error bits: %x",r->cr2,r->err);
            else api->kfmt(b,sizeof b,"Error bits: %x",r->err);
        }
        api->draw_text_clip(cx + 8, cy + ch - 20, b, C_NAVY, cw - 16);
    } else {
        api->draw_text(cx + 8, cy + ch - 18,
                       "Select a row. Scroll for older faults.", C_GRAY);
    }
}

static void fl_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)lx; (void)cw;
    if (ev != EV_PRESS || ly < LIST_TOP || ly >= LIST_TOP+list_rows(ch)*ROWH) return;
    int i = top+(ly - LIST_TOP) / ROWH;
    int n = api->fault_count();
    if (i < 0 || i >= n) return;
    const FaultRec *r = api->fault_get(i);
    if (!r) return;
    if (sel_valid && sel_row(n) == i) sel_valid = 0;
    else { sel_sequence = r->sequence; sel_valid = 1; }
    api->gui_dirty();
}

static void fl_wheel(int inst,int delta)
{
    (void)inst;top-=delta;
    if(top<0)top=0;
    if(top>=api->fault_count())top=api->fault_count()-1;
    if(top<0)top=0;
    api->gui_dirty();
}

static void fl_open(int inst)
{
    (void)inst;
    sel_valid = 0;top=0;
    if (timer_id < 0) timer_id = api->timer_add(50, tick, 0);
}
static void fl_close(int inst)
{
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

static void fl_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, KEXT_RECLAIMABLE, "Fault Log"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Fault Log", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = fl_open, .close = fl_close, .draw = fl_draw, .mouse = fl_mouse,
        .client_size = fl_csize, .category = APP_CAT_DEV, .wheel = fl_wheel,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
