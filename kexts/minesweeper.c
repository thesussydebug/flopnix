#include "kapi.h"
#include "mine_core.inc"

static const Kapi *api;
static int my_type = -1;

#define CELL   18
#define M      10
#define HDR    36
#define TOP    HDR
#define MN_W   9
#define MN_H   9
#define MINES  10

static MnBoard B;
static int started, dead, won;
static int lose_i = -1;
static u32 t_start;
static int final_t;

static const u8 numcol[9] = {
    0, C_BBLUE, C_GREEN, C_RED, C_NAVY, C_MAROON, C_TEAL, C_BLACK, C_GRAY
};

static int grid_w(void) { return MN_W * CELL; }
static int grid_h(void) { return MN_H * CELL; }
static int win_w(void)  { return grid_w() + 2 * M; }
static int win_h(void)  { return TOP + grid_h() + M; }
static int grid_x(int cw) { return (cw - grid_w()) / 2; }

static void new_game(void)
{
    mn_reset(&B, MN_W, MN_H, MINES);
    started = dead = won = 0;
    lose_i = -1;
    final_t = 0;
}

static void ms_open(int inst) { (void)inst; new_game(); }

static u32 rng(void) { return api->rand(); }

static void stop_clock(void)
{
    final_t = (int)((*api->ticks - t_start) / 100);
}

static void check_end(void)
{
    if (!dead && mn_won(&B)) {
        won = 1;
        stop_clock();

        for (int i = 0; i < B.w * B.h; i++)
            if (B.mine[i]) B.mark[i] = MN_FLAG;
    }
}

static void dig(int r, int c)
{
    if (dead || won) return;
    int i = mn_i(&B, r, c);
    if (B.mark[i] == MN_FLAG || B.shown[i]) return;
    if (!started) {
        mn_place(&B, r, c, rng);
        started = 1;
        t_start = *api->ticks;
    }
    if (B.mine[i]) {
        B.shown[i] = 1;
        dead = 1;
        lose_i = i;
        stop_clock();
        return;
    }
    mn_reveal(&B, r, c);
    check_end();
}

static void chord(int r, int c)
{
    if (dead || won || !started) return;
    int hit = 0;
    if (!mn_chord(&B, r, c, &hit)) return;
    if (hit) {
        dead = 1;
        for (int i = 0; i < B.w * B.h; i++)
            if (B.mine[i] && B.shown[i]) { lose_i = i; break; }
        stop_clock();
        return;
    }
    check_end();
}

static int over;

static void draw_face(int x, int y)
{
    int cx = x + 12, cy = y + 12;
    int hov = over && *api->mouse_x >= x && *api->mouse_x < x + 24 &&
              *api->mouse_y >= y && *api->mouse_y < y + 24;
    api->panel(x, y, 24, 24, hov);
    api->fill_circle(cx, cy, 8, C_YELLOW);
    api->circle(cx, cy, 8, C_BLACK);
    if (dead) {
        api->draw_char(cx - 6, cy - 8, 'x', C_BLACK);
        api->draw_char(cx + 1, cy - 8, 'x', C_BLACK);
        api->fill_rect(cx - 3, cy + 5, 7, 1, C_BLACK);
        api->pixel(cx - 4, cy + 4, C_BLACK);
        api->pixel(cx + 4, cy + 4, C_BLACK);
    } else if (won) {
        api->fill_rect(cx - 6, cy - 3, 5, 3, C_BLACK);
        api->fill_rect(cx + 1, cy - 3, 5, 3, C_BLACK);
        api->fill_rect(cx - 1, cy - 2, 2, 1, C_BLACK);
        api->fill_rect(cx - 3, cy + 3, 7, 1, C_BLACK);
        api->pixel(cx - 4, cy + 2, C_BLACK);
        api->pixel(cx + 4, cy + 2, C_BLACK);
    } else {
        api->fill_rect(cx - 4, cy - 3, 2, 2, C_BLACK);
        api->fill_rect(cx + 3, cy - 3, 2, 2, C_BLACK);
        api->fill_rect(cx - 3, cy + 3, 7, 1, C_BLACK);
        api->pixel(cx - 4, cy + 2, C_BLACK);
        api->pixel(cx + 4, cy + 2, C_BLACK);
    }
}

static void lcd(int x, int y, int val)
{
    static const u8 digits[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };

    static const u8 seg[7][4] = {
        {2,0,6,2}, {8,2,2,5}, {8,9,2,5}, {2,14,6,2},
        {0,9,2,5}, {0,2,2,5}, {2,7,6,2}
    };
    api->panel(x, y, 40, 22, 1);
    api->fill_rect(x + 2, y + 2, 36, 18, C_BLACK);
    if (val < -99) val = -99;
    if (val > 999) val = 999;
    int negative = val < 0;
    if (negative) val = -val;
    u8 masks[3] = { negative ? 0x40 : digits[val / 100],
                    digits[(val / 10) % 10], digits[val % 10] };
    for (int d = 0; d < 3; d++)
        for (int s = 0; s < 7; s++)
            if (masks[d] & (1 << s))
                api->fill_rect(x + 3 + d * 12 + seg[s][0], y + 3 + seg[s][1],
                               seg[s][2], seg[s][3], C_RED);
}

static void mine_glyph(int x, int y)
{
    api->fill_circle(x + 9, y + 9, 5, C_BLACK);
    api->fill_rect(x + 8, y + 4, 2, 11, C_BLACK);
    api->fill_rect(x + 3, y + 9, 12, 2, C_BLACK);
    api->pixel(x + 7, y + 7, C_WHITE);
}

static void flag_glyph(int x, int y)
{
    api->fill_rect(x + 10, y + 4, 1, 10, C_BLACK);
    api->fill_rect(x + 5, y + 4, 6, 4, C_RED);
    api->fill_rect(x + 6, y + 13, 8, 2, C_BLACK);
}

static void ms_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)ch;
    over = api->win_is_hovered(w);
    api->fill_rect(cx, cy, cw, ch, C_FACE);

    lcd(cx + M, cy + 7, MINES - mn_count_mark(&B, MN_FLAG));
    draw_face(cx + cw / 2 - 12, cy + 6);
    int t = (started && !dead && !won) ? (int)((*api->ticks - t_start) / 100)
                                       : final_t;
    lcd(cx + cw - M - 40, cy + 7, t);

    int ox = cx + grid_x(cw), oy = cy + TOP;

    api->panel(ox - 2, oy - 2, grid_w() + 4, grid_h() + 4, 1);
    for (int r = 0; r < B.h; r++)
        for (int c = 0; c < B.w; c++) {
            int i = mn_i(&B, r, c);
            int x = ox + c * CELL, y = oy + r * CELL;

            int show_mine = dead && B.mine[i] && B.mark[i] != MN_FLAG;
            if (B.shown[i] || show_mine) {
                u8 bg = (i == lose_i) ? C_RED : C_G0 + 3;
                api->fill_rect(x, y, CELL, CELL, bg);
                api->hline(x, y, CELL, C_G0 + 1);
                api->vline(x, y, CELL, C_G0 + 1);
                if (B.mine[i]) mine_glyph(x, y);
                else if (B.adj[i]) {
                    char n[2] = { (char)('0' + B.adj[i]), 0 };
                    api->draw_text(x + 5, y + 1, n, numcol[B.adj[i]]);
                }
            } else {
                api->panel(x, y, CELL, CELL, 0);
                if (B.mark[i] == MN_FLAG) {
                    flag_glyph(x, y);
                    if (dead && !B.mine[i]) {
                        api->line(x + 3, y + 3, x + CELL - 4, y + CELL - 4, C_RED);
                        api->line(x + CELL - 4, y + 3, x + 3, y + CELL - 4, C_RED);
                    }
                } else if (B.mark[i] == MN_QUERY) {
                    api->draw_text(x + 5, y + 1, "?", C_NAVY);
                }
            }
        }
}

static void ms_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)ch;
    (void)inst;
    if (ev != EV_PRESS && ev != EV_RPRESS) return;
    int fx = cw / 2 - 12;
    if (ev == EV_PRESS && ly >= 6 && ly < 30 && lx >= fx && lx < fx + 24) {
        new_game();
        return;
    }
    int ox = grid_x(cw), oy = TOP;
    if (lx < ox || ly < oy) return;
    int c = (lx - ox) / CELL, r = (ly - oy) / CELL;
    if (c < 0 || c >= B.w || r < 0 || r >= B.h) return;
    if (ev == EV_RPRESS) {
        if (!dead && !won) mn_cycle_mark(&B, r, c);
    } else if (B.shown[mn_i(&B, r, c)]) {
        chord(r, c);
    } else {
        dig(r, c);
    }
}

static void ms_key(int inst, int k)
{
    (void)inst;
    if (k == '\n' || k == ' ' || k == 'r' || k == 'R') new_game();
    api->gui_dirty();
}

static void ms_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = win_w();
    *h = win_h();
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Minesweeper"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    new_game();
    static const AppDesc d = {
        .title = "Minesweeper", .max_inst = 1, .in_menu = 1,
        .open = ms_open, .draw = ms_draw, .mouse = ms_mouse, .key = ms_key,
        .client_size = ms_csize, .category = APP_CAT_GAMES,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
