#include "kapi.h"

static const Kapi *api;
static int my_type = -1;

#define WINW 240
#define WINH 346
#define ROWH 26
#define BX   12
#define BW   (WINW - 24)
#define BY0  40

static void c_div0(void) { volatile int a = 1, b = 0; volatile int c = a / b; (void)c; }
static void c_bp(void)   { __asm__ volatile("int3"); }
static void c_of(void)   { __asm__ volatile("movl $0x7fffffff, %%eax\n\t"
                                            "addl $1, %%eax\n\t"
                                            "into" ::: "eax"); }
static void c_ud(void)   { __asm__ volatile("ud2"); }
static void c_gp(void)   { __asm__ volatile("movl $0xFFF8, %%eax\n\t"
                                            "movl %%eax, %%gs" ::: "eax"); }
static void c_null(void) { volatile int *p = 0; *p = 1; }

static void c_slow(void)
{
    u32 t0 = *api->ticks;
    while ((u32)(*api->ticks - t0) < 200) ;
}

static void c_hang(void)
{
    for (;;) ;
}

static void c_hangpump(void)
{
    for (;;) api->gui_pump();
}

static void c_slowpump(void)
{
    u32 t0 = *api->ticks;
    while ((u32)(*api->ticks - t0) < 500) api->gui_pump();
    api->notify("slow work finished normally");
}

#define EAT_MAX 512
static void *eaten[EAT_MAX];
static int  neaten;
static void cs_close(int inst)
{
    (void)inst;
    while (neaten) api->kfree(eaten[--neaten]);
}

static void c_eatheap(void)
{
    if (neaten) {
        cs_close(0);
        api->notify("heap released");
        return;
    }
    u32 sz = 65536;
    while (neaten < EAT_MAX) {
        void *p = api->kmalloc(sz);
        if (!p) { if (sz <= 64) break; sz /= 2; continue; }
        eaten[neaten++] = p;
    }
    char m[48];
    api->kfmt(m, sizeof m, "heap eaten - %u bytes left", api->heap_avail());
    api->notify(m);
}

static const struct { const char *label; void (*go)(void); } crashes[] = {
    { "Divide by zero    (P0)",   c_div0 },
    { "Breakpoint int3   (P3)",   c_bp   },
    { "Overflow into     (P4)",   c_of   },
    { "Invalid opcode    (P6)",   c_ud   },
    { "Access viol / GP  (P13)",  c_gp   },
    { "Null dereference  (P14)",  c_null },
    { "Slow handler (2s stall)",  c_slow },
    { "Hang forever (wedge)",     c_hang },
    { "Hang in a device wait",    c_hangpump },
    { "Slow honest work (5s)",    c_slowpump },
    { "Eat heap (toggle)",        c_eatheap },
};
#define NCRASH ((int)(sizeof crashes / sizeof crashes[0]))

static void cs_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w; (void)cw; (void)ch;
    api->fill_rect(cx, cy, WINW, WINH, C_FACE);
    api->draw_text(cx + 10, cy + 8, "Trigger a fault to test", C_BLACK);
    api->draw_text(cx + 10, cy + 20, "kernel crash recovery:", C_NAVY);
    for (int i = 0; i < NCRASH; i++) {
        int by = cy + BY0 + i * ROWH;
        int hov = api->win_is_hovered(w) && api->mouse_x && *api->mouse_x >= cx + BX &&
                  *api->mouse_x < cx + BX + BW &&
                  *api->mouse_y >= by && *api->mouse_y < by + ROWH - 4;
        api->panel(cx + BX, by, BW, ROWH - 4, 0);
        if (hov) api->fill_rect(cx + BX + 2, by + 2, BW - 4, ROWH - 8, C_HILITE);
        api->draw_text(cx + BX + 8, by + 4, crashes[i].label,
                       hov ? C_WHITE : C_BLACK);
    }
}

static void cs_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    for (int i = 0; i < NCRASH; i++) {
        int by = BY0 + i * ROWH;
        if (lx >= BX && lx < BX + BW && ly >= by && ly < by + ROWH - 4) {
            crashes[i].go();
            return;
        }
    }
}

static void cs_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Crash Test"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    static const AppDesc d = {
        .title = "Crash Test", .max_inst = 1, .in_menu = 1,
        .draw = cs_draw, .mouse = cs_mouse, .client_size = cs_csize, .close = cs_close,
        .category = APP_CAT_DEV,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
