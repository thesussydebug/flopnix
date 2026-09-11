#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;

#include "pong_core.inc"

#define WINW PONG_W
#define WINH PONG_H
#define AI_STEP 3
#define WIN_AT 10

static PongSt st;
static int sl, sr;
static int running, over;
static int last_dir = -1;

static int held_dir(void)
{
    int up = api->key_down(K_UP)   || api->key_down('w');
    int dn = api->key_down(K_DOWN) || api->key_down('s');
    if (up && dn) return last_dir;
    if (up) return -1;
    if (dn) return 1;
    return 0;
}

static int is_open(void)
{
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == my_type) return 1;
    }
    return 0;
}

static int beep_left;

static void beep(u32 hz, int ticks)
{
    api->speaker_tone(hz);
    beep_left = ticks;
}

static void tick(void *ctx)
{
    (void)ctx;

    if (beep_left && --beep_left == 0) api->speaker_off();
    if (!is_open() || !running || over) return;
    int dir = held_dir();
    if (dir) {
        st.pl += dir * 4;
        if (st.pl < 0) st.pl = 0;
        if (st.pl > PONG_H - PONG_PH) st.pl = PONG_H - PONG_PH;
    }
    pong_ai(&st, AI_STEP);
    int r = pong_step(&st);

    if (r)                            beep(180, 12);
    else if (st.hit & PONG_HIT_PADDLE) beep(880, 3);
    else if (st.hit & PONG_HIT_WALL)   beep(440, 2);

    if (r) {
        if (r == 1) sl++; else sr++;
        running = 0;
        if (sl >= WIN_AT || sr >= WIN_AT) over = 1;
        pong_serve(&st, r == 1 ? 1 : -1);
    }
    api->gui_dirty();
}

static void pong_close(int inst)
{
    (void)inst;
    beep_left = 0;
    api->speaker_off();
}

static void pong_open(int inst)
{
    (void)inst;
    pong_serve(&st, 1);
    sl = sr = 0;
    running = over = 0;
    last_dir = -1;
    if (timer_id < 0) timer_id = api->timer_add(2, tick, 0);
}

static void pong_key(int inst, int k)
{
    (void)inst;
    switch (k) {

    case K_UP:   case 'w': case 'W':
        last_dir = -1;
        if (!over) running = 1;
        break;
    case K_DOWN: case 's': case 'S':
        last_dir = 1;
        if (!over) running = 1;
        break;
    case ' ':
        if (!over) running = !running;
        break;
    case 'r': case 'R':
        pong_serve(&st, 1);
        sl = sr = 0;
        running = over = 0;
        break;
    }
    api->gui_dirty();
}

static void num(char *b, int cap, int v)
{
    api->kfmt(b, cap, "%d", v);
}

static void pong_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w; (void)cw; (void)ch;
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, WINW, WINH,
                           GRGB(10, 14, 30), GRGB(4, 6, 14), 1);
        gfx->set_dither(0);
    } else api->fill_rect(cx, cy, WINW, WINH, C_BLACK);

    for (int y = 0; y < WINH; y += 12)
        api->fill_rect(cx + WINW / 2 - 1, cy + y, 2, 6, C_G0 + 4);

    char b[8];
    num(b, sizeof b, sl);
    api->draw_text_scaled(cx + WINW / 2 - 40, cy + 8, b, C_WHITE, 2, 2);
    num(b, sizeof b, sr);
    api->draw_text_scaled(cx + WINW / 2 + 26, cy + 8, b, C_WHITE, 2, 2);

    api->fill_rect(cx + PONG_PX, cy + st.pl, PONG_PW, PONG_PH, C_WHITE);
    api->fill_rect(cx + WINW - PONG_PX - PONG_PW, cy + st.pr,
                   PONG_PW, PONG_PH, C_CYAN);
    api->fill_rect(cx + (st.bx >> 8), cy + (st.by >> 8),
                   PONG_B, PONG_B, C_YELLOW);

    if (over) {
        const char *msg = sl > sr ? "YOU WIN!  R to reset"
                                  : "AI WINS.  R to reset";
        api->fill_rect(cx + WINW / 2 - 90, cy + WINH / 2 - 12, 180, 24, C_BLACK);
        api->draw_text(cx + WINW / 2 - (int)api->strlen(msg) * 4,
                       cy + WINH / 2 - 4, msg, C_BGREEN);
    } else if (!running) {
        const char *msg = "W/S or arrows - Space serves";
        api->draw_text(cx + WINW / 2 - (int)api->strlen(msg) * 4,
                       cy + WINH - 20, msg, C_G0 + 6);
    }
}

static void pong_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = WINW;
    *h = WINH;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Pong"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Pong", .max_inst = 1, .in_menu = 1,
        .open = pong_open, .draw = pong_draw,
        .key = pong_key, .client_size = pong_csize,
        .category = APP_CAT_GAMES,
        .close = pong_close,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
