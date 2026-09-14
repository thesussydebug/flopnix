#include "kapi.h"
#include "gdi.h"
#include "snake_core.inc"

static const Kapi *api;
static const GdiOps *gfx;

#define GW    26
#define GH    20
#define CELL  14
#define HDR   28
#define M     8
#define START 4
#define MAXCELLS (GW * GH)

static const int DX[4] = { 0, 0, -1, 1 };
static const int DY[4] = { -1, 1, 0, 0 };

static u8  grid[GH][GW];
static i16 snx[MAXCELLS], sny[MAXCELLS];
static int len;
static int dir, next_dir;
static int foodx, foody;
static int score;
static int subtick;
static u8  started, paused, over, won;

static int snake_type = -1;
static int timer_id = -1;
static u32 beep_off_at;

static void blip(u32 hz, u32 ticks_len)
{
    api->speaker_tone(hz);
    beep_off_at = *api->ticks + ticks_len;
    if (!beep_off_at) beep_off_at = 1;
}

static void place_food(void)
{
    int x0, y0, x1, y1;
    snake_food_box(score, GW, GH, &x0, &y0, &x1, &y1);

    for (int pass = 0; pass < 2; pass++) {
        if (pass) { x0 = 0; y0 = 0; x1 = GW; y1 = GH; }
        int empties = 0;
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (!grid[y][x]) empties++;
        if (!empties) continue;
        int k = (int)(api->rand() % (u32)empties);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                if (!grid[y][x] && k-- == 0) { foodx = x; foody = y; return; }
    }
    foodx = foody = -1;
}

static void reset(int inst)
{
    (void)inst;
    api->memset(grid, 0, sizeof grid);
    len = START;
    int hx = GW / 2, hy = GH / 2;
    for (int i = 0; i < len; i++) {
        snx[i] = (i16)(hx - i);
        sny[i] = (i16)hy;
        grid[hy][hx - i] = 1;
    }
    dir = next_dir = 3;
    score = 0;
    subtick = 0;
    started = paused = over = won = 0;
    place_food();
}

static int step_ticks(void)
{
    int s = 6 - score / 5;
    return s < 3 ? 3 : s;
}

static void on_over(int result, void *ctx)
{
    (void)ctx;
    if (result == MBR_YES) { reset(0); api->gui_dirty(); }
}

static void game_over(void)
{
    over = 1;
    blip(140, 24);
    api->gui_dirty();
    char m[52];
    api->kfmt(m, sizeof m, "You scored %d.  Play again?", score);
    api->msgbox("Game Over", m, MB_YESNO, on_over, 0);
}

static void game_won(void)
{
    over = won = 1;
    api->gui_dirty();
    api->notify("You filled the whole board");
    api->msgbox("You win!", "You filled the whole board!  Play again?",
                MB_YESNO, on_over, 0);
}

static void advance(void)
{
    dir = next_dir;
    int nx = snx[0] + DX[dir], ny = sny[0] + DY[dir];
    if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) { game_over(); return; }

    int grow = (nx == foodx && ny == foody);
    int tx = snx[len - 1], ty = sny[len - 1];
    if (grid[ny][nx] && !(!grow && nx == tx && ny == ty)) { game_over(); return; }

    if (grow) {
        api->memmove(snx + 1, snx, (u32)len * sizeof(i16));
        api->memmove(sny + 1, sny, (u32)len * sizeof(i16));
        snx[0] = (i16)nx; sny[0] = (i16)ny; grid[ny][nx] = 1;
        len++;
        score++;
        blip(880, 3);
        if (len >= MAXCELLS) { game_won(); return; }
        place_food();
    } else {
        grid[ty][tx] = 0;
        api->memmove(snx + 1, snx, (u32)(len - 1) * sizeof(i16));
        api->memmove(sny + 1, sny, (u32)(len - 1) * sizeof(i16));
        snx[0] = (i16)nx; sny[0] = (i16)ny; grid[ny][nx] = 1;
    }
}

static int snake_is_open(void)
{
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == snake_type) return 1;
    }
    return 0;
}

static void tick(void *ctx)
{
    (void)ctx;
    if (beep_off_at && *api->ticks >= beep_off_at) { api->speaker_off(); beep_off_at = 0; }
    if (!snake_is_open() || !started || paused || over) return;
    if (++subtick < step_ticks()) return;
    subtick = 0;
    advance();
    api->gui_dirty();
}

static void turn_to(int nd)
{
    if (over) return;
    started = 1;
    if (nd != (dir ^ 1)) next_dir = nd;

}

static void snake_key(int inst, int k)
{
    (void)inst;
    switch (k) {
    case K_UP:    case 'w': case 'W': turn_to(0); break;
    case K_DOWN:  case 's': case 'S': turn_to(1); break;
    case K_LEFT:  case 'a': case 'A': turn_to(2); break;
    case K_RIGHT: case 'd': case 'D': turn_to(3); break;
    case ' ': case 'p': case 'P':
        if (started && !over) { paused = !paused; api->gui_dirty(); }
        break;
    }
}

static void draw_head_eyes(int x, int y)
{
    int a, b, c, d;
    switch (dir) {
    case 0:  a = x + 3; b = y + 2; c = x + CELL - 5; d = y + 2; break;
    case 1:  a = x + 3; b = y + CELL - 4; c = x + CELL - 5; d = y + CELL - 4; break;
    case 2:  a = x + 2; b = y + 3; c = x + 2; d = y + CELL - 5; break;
    default: a = x + CELL - 4; b = y + 3; c = x + CELL - 4; d = y + CELL - 5; break;
    }
    api->fill_rect(a, b, 2, 2, C_BLACK);
    api->fill_rect(c, d, 2, 2, C_BLACK);
}

static int hover_ok;

static void hdr_btn(int x, int y, int w, const char *label)
{
    int hov = hover_ok && *api->mouse_x >= x && *api->mouse_x < x + w &&
              *api->mouse_y >= y && *api->mouse_y < y + 20;
    api->panel(x, y, w, 20, hov);
    api->draw_text(x + (w - (int)api->strlen(label) * 8) / 2, y + 4, label, C_BLACK);
}

static u32 seg_rgb(int i)
{
    int t = len > 1 ? (i * 256) / (len - 1) : 0;
    int r = 80 - ((26 * t) >> 8);
    int g = 210 - ((48 * t) >> 8);
    int b = 95 - ((24 * t) >> 8);
    return GRGB(r, g, b);
}

static void snake_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)ch;
    hover_ok = api->win_is_hovered(w);

    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, HDR, GRGB(214, 218, 228), GRGB(180, 186, 202), 1);
        gfx->set_dither(0);
    } else
        api->fill_rect(cx, cy, cw, HDR, C_FACE);
    hdr_btn(cx + M, cy + 4, 44, "New");
    hdr_btn(cx + M + 52, cy + 4, 56, paused ? "Resume" : "Pause");
    char b[24];
    api->kfmt(b, sizeof b, "Score: %d", score);
    api->draw_text(cx + M + 118, cy + 8, b, C_NAVY);
    const char *st = over ? (won ? "you win!" : "game over")
                   : !started ? "press an arrow"
                   : paused ? "paused" : "playing";
    api->draw_text(cx + cw - (int)api->strlen(st) * 8 - M, cy + 8, st,
                   over ? C_MAROON : C_G0 + 3);

    int px = cx + M, py = cy + HDR, pw = GW * CELL, ph = GH * CELL;
    api->panel(px - 2, py - 2, pw + 4, ph + 4, 1);
    if (!gfx) {
        api->fill_rect(px, py, pw, ph, C_G0 + 1);
        if (foodx >= 0) {
            int fx = px + foodx * CELL + CELL / 2, fy = py + foody * CELL + CELL / 2;
            api->fill_circle(fx, fy, CELL / 2 - 2, C_RED);
            api->fill_circle(fx - 1, fy - 1, 1, C_YELLOW);
        }
        for (int i = 0; i < len; i++) {
            int x = px + snx[i] * CELL, y = py + sny[i] * CELL;
            api->fill_rect(x + 1, y + 1, CELL - 2, CELL - 2, i == 0 ? C_BGREEN : C_GREEN);
        }
        draw_head_eyes(px + snx[0] * CELL, py + sny[0] * CELL);
        return;
    }

    gfx->set_dither(1);
    gfx->fill_gradient(px, py, pw, ph, GRGB(28, 36, 42), GRGB(12, 16, 20), 1);
    gfx->set_dither(0);

    if (foodx >= 0) {
        int fx = px + foodx * CELL + CELL / 2, fy = py + foody * CELL + CELL / 2;
        int r = CELL / 2 - 2;
        static const signed char c12[12][2] = {
            {32,0},{28,16},{16,28},{0,32},{-16,28},{-28,16},
            {-32,0},{-28,-16},{-16,-28},{0,-32},{16,-28},{28,-16}
        };
        GPt pel[12];
        for (int i = 0; i < 12; i++) {
            pel[i].x = fx + c12[i][0] * r / 32;
            pel[i].y = fy + c12[i][1] * r / 32;
        }
        gfx->fill_poly_aa(pel, 12, GRGB(225, 60, 50));
        gfx->fill_rgb(fx - 2, fy - 2, 2, 2, GRGB(255, 235, 150));
    }

    for (int i = len - 2; i >= 1; i--) {
        int x = px + snx[i] * CELL, y = py + sny[i] * CELL;
        gfx->fill_rgb(x + 1, y + 1, CELL - 2, CELL - 2, seg_rgb(i));
    }
    {

        int t = len - 1;
        int x = px + snx[t] * CELL, y = py + sny[t] * CELL;
        int dx = snx[t] - snx[t - 1], dy = sny[t] - sny[t - 1];
        GPt tri[3];
        if (dx > 0) {
            tri[0] = (GPt){ x + 1, y + 1 };
            tri[1] = (GPt){ x + 1, y + CELL - 1 };
            tri[2] = (GPt){ x + CELL - 1, y + CELL / 2 };
        } else if (dx < 0) {
            tri[0] = (GPt){ x + CELL - 1, y + 1 };
            tri[1] = (GPt){ x + CELL - 1, y + CELL - 1 };
            tri[2] = (GPt){ x + 1, y + CELL / 2 };
        } else if (dy > 0) {
            tri[0] = (GPt){ x + 1, y + 1 };
            tri[1] = (GPt){ x + CELL - 1, y + 1 };
            tri[2] = (GPt){ x + CELL / 2, y + CELL - 1 };
        } else {
            tri[0] = (GPt){ x + 1, y + CELL - 1 };
            tri[1] = (GPt){ x + CELL - 1, y + CELL - 1 };
            tri[2] = (GPt){ x + CELL / 2, y + 1 };
        }
        gfx->fill_poly_aa(tri, 3, seg_rgb(t));
    }
    {
        int x = px + snx[0] * CELL, y = py + sny[0] * CELL;
        gfx->fill_rgb(x + 1, y + 1, CELL - 2, CELL - 2, GRGB(110, 235, 130));
        draw_head_eyes(x, y);
    }
}

static void snake_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    if (ly < 4 || ly >= 24) return;
    if (lx >= M && lx < M + 44) { reset(0); api->gui_dirty(); return; }
    if (lx >= M + 52 && lx < M + 108) {
        if (started && !over) { paused = !paused; api->gui_dirty(); }
    }
}

static void snake_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 2 * M + GW * CELL;
    *h = HDR + GH * CELL + M;
}

static void snake_open(int inst)
{
    reset(inst);
    if (timer_id < 0) timer_id = api->timer_add(2, tick, 0);
}
static void snake_close(int inst)
{
    (void)inst;
    if (beep_off_at) { api->speaker_off(); beep_off_at = 0; }
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Snake"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Snake", .max_inst = 1, .in_menu = 1,
        .open = snake_open, .close = snake_close, .draw = snake_draw,
        .key = snake_key, .mouse = snake_mouse, .client_size = snake_csize,
    };
    snake_type = k->register_app(&d);
    return snake_type < 0;
}
