#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;

#include "tetris_core.inc"

#define CELL   12
#define BX     8
#define BY     8
#define PANEL  86
#define WINW   (BX * 2 + TET_W * CELL + PANEL)
#define WINH   (BY * 2 + TET_H * CELL)

static u8  board[TET_W * TET_H];
static int piece, rot, px, py;
static int next_piece;
static int score, lines, level;
static volatile u8 in_lock;
static int fall;
static int started, paused, over;

static const u8 piece_col[7] = {
    C_CYAN, C_YELLOW, C_MAGENTA, C_BGREEN, C_RED, C_BBLUE, C_G0 + 6
};

static int is_open(void)
{
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == my_type) return 1;
    }
    return 0;
}

static void new_piece(void)
{
    piece = next_piece;
    next_piece = (int)(api->rand() % 7);
    rot = 0;
    px = TET_W / 2 - 2;
    py = 0;
    fall = tet_speed(level);
    if (!tet_fits(board, piece, rot, px, py)) over = 1;
}

static void reset(void)
{
    for (int i = 0; i < TET_W * TET_H; i++) board[i] = 0;
    score = lines = level = 0;
    started = paused = over = 0;
    in_lock = 0;
    next_piece = (int)(api->rand() % 7);
    new_piece();
}

static void lock_down(void)
{
    if (in_lock) return;

    if (tet_fits(board, piece, rot, px, py + 1)) return;
    in_lock = 1;
    tet_lock(board, piece, rot, px, py);
    int n = tet_clear(board);
    if (n) {
        lines += n;
        score += tet_score(n, level);
        level = tet_level(lines);
    }
    new_piece();
    in_lock = 0;
}

static void drop_one(void)
{
    if (tet_fits(board, piece, rot, px, py + 1)) py++;
    else lock_down();
}

static void hard_drop(void)
{
    py = tet_drop_y(board, piece, rot, px, py);
    lock_down();
}

static void tick(void *ctx)
{
    (void)ctx;
    if (!is_open() || !started || paused || over) return;
    if (--fall > 0) return;
    fall = tet_speed(level);
    drop_one();
    api->gui_dirty();
}

static void tetris_open(int inst)
{
    (void)inst;
    reset();
    if (timer_id < 0) timer_id = api->timer_add(2, tick, 0);
}
static void tetris_close(int inst)
{
    (void)inst;
    if (timer_id >= 0) { api->timer_del(timer_id); timer_id = -1; }
}

static void tetris_key(int inst, int k)
{
    (void)inst;
    if (over) {
        if (k == 'r' || k == 'R') reset();
        api->gui_dirty();
        return;
    }

    if (paused) {
        if (k == 'p' || k == 'P') paused = 0;
        else if (k == 'r' || k == 'R') reset();
        api->gui_dirty();
        return;
    }
    switch (k) {
    case K_LEFT:  case 'a': case 'A':
        started = 1;
        if (tet_fits(board, piece, rot, px - 1, py)) px--;
        break;
    case K_RIGHT: case 'd': case 'D':
        started = 1;
        if (tet_fits(board, piece, rot, px + 1, py)) px++;
        break;
    case K_UP:    case 'w': case 'W':
        started = 1;
        if (tet_fits(board, piece, rot + 1, px, py)) rot = (rot + 1) & 3;
        break;
    case K_DOWN:  case 's': case 'S':
    case ' ':
        if (!started) { started = 1; break; }
        if (!paused) hard_drop();
        break;
    case 'p': case 'P':
        if (started) paused = !paused;
        break;
    case 'r': case 'R':
        reset();
        break;
    }
    api->gui_dirty();
}

static void cell(int cx, int cy, int x, int y, u8 col)
{
    int sx = cx + BX + x * CELL, sy = cy + BY + y * CELL;
    api->fill_rect(sx, sy, CELL - 1, CELL - 1, col);
    api->fill_rect(sx, sy, CELL - 1, 1, C_WHITE);
    api->fill_rect(sx, sy, 1, CELL - 1, C_WHITE);
}

static void draw_mask(int cx, int cy, u16 m, int ox, int oy, u8 col)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (m & (1 << (y * 4 + x)))
                cell(cx, cy, ox + x, oy + y, col);
}

static void ghost_cell(int cx, int cy, int x, int y, u8 col)
{
    int sx = cx + BX + x * CELL, sy = cy + BY + y * CELL;
    for (int dy = 0; dy < CELL - 1; dy++)
        for (int dx = (dy & 1); dx < CELL - 1; dx += 2)
            api->pixel(sx + dx, sy + dy, col);
}

static void draw_ghost(int cx, int cy)
{
    int gy = tet_drop_y(board, piece, rot, px, py);
    if (gy == py) return;
    u16 m = tet_mask[piece][rot];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (m & (1 << (y * 4 + x)))
                ghost_cell(cx, cy, px + x, gy + y, piece_col[piece]);
}

static void tetris_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w; (void)cw; (void)ch;
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, WINW, WINH,
                           GRGB(24, 20, 44), GRGB(8, 6, 18), 1);
        gfx->set_dither(0);
    } else api->fill_rect(cx, cy, WINW, WINH, C_BLACK);

    api->fill_rect(cx + BX - 2, cy + BY - 2,
                   TET_W * CELL + 3, TET_H * CELL + 3, C_BLACK);
    for (int y = 0; y < TET_H; y++)
        for (int x = 0; x < TET_W; x++)
            if (board[y * TET_W + x])
                cell(cx, cy, x, y, piece_col[board[y * TET_W + x] - 1]);

    if (!over) {
        if (started && !paused) draw_ghost(cx, cy);
        draw_mask(cx, cy, tet_mask[piece][rot], px, py, piece_col[piece]);
    }

    int panx = cx + BX + TET_W * CELL + 10, pany = cy + BY;
    char b[24];
    api->draw_text(panx, pany, "SCORE", C_G0 + 6);
    api->kfmt(b, sizeof b, "%d", score);
    api->draw_text(panx, pany + 12, b, C_WHITE);
    api->draw_text(panx, pany + 34, "LINES", C_G0 + 6);
    api->kfmt(b, sizeof b, "%d", lines);
    api->draw_text(panx, pany + 46, b, C_WHITE);
    api->draw_text(panx, pany + 68, "LEVEL", C_G0 + 6);
    api->kfmt(b, sizeof b, "%d", level);
    api->draw_text(panx, pany + 80, b, C_WHITE);
    api->draw_text(panx, pany + 102, "NEXT", C_G0 + 6);
    draw_mask(cx, cy, tet_mask[next_piece][0],
              TET_W + 1, 10, piece_col[next_piece]);

    if (over) {
        api->fill_rect(cx + BX + 4, cy + WINH / 2 - 14,
                       TET_W * CELL - 8, 28, C_BLACK);
        api->draw_text(cx + BX + 14, cy + WINH / 2 - 8, "GAME OVER", C_RED);
        api->draw_text(cx + BX + 14, cy + WINH / 2 + 2, "R restarts", C_G0 + 6);
    } else if (paused) {
        api->draw_text(cx + BX + 34, cy + WINH / 2 - 4, "PAUSED", C_YELLOW);
    } else if (!started) {
        api->draw_text(panx, pany + 150, "any key", C_G0 + 6);
        api->draw_text(panx, pany + 162, "starts", C_G0 + 6);
    }
}

static void tetris_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = WINW;
    *h = WINH;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, KEXT_RECLAIMABLE, "Tetris"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {.live_draw=APP_LIVE_DRAW|APP_INDEPENDENT,
        .title = "Tetris", .max_inst = 1, .in_menu = 1,
        .open = tetris_open, .close = tetris_close, .draw = tetris_draw,
        .key = tetris_key, .client_size = tetris_csize,
        .category = APP_CAT_GAMES,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
