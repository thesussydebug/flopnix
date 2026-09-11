#include "kapi.h"
#include "gdi.h"

static const Kapi *api;
static const GdiOps *gfx;

#define CELL 30
#define M    10
#define HDR  28
#define OX   M
#define OY   HDR

static u8 board[8][8];
static int turn;
static int over;

static const int DR[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static const int DC[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };

static const int WT[8][8] = {
    { 100, -20, 10,  5,  5, 10, -20, 100 },
    { -20, -50, -2, -2, -2, -2, -50, -20 },
    {  10,  -2, -1, -1, -1, -1,  -2,  10 },
    {   5,  -2, -1, -1, -1, -1,  -2,   5 },
    {   5,  -2, -1, -1, -1, -1,  -2,   5 },
    {  10,  -2, -1, -1, -1, -1,  -2,  10 },
    { -20, -50, -2, -2, -2, -2, -50, -20 },
    { 100, -20, 10,  5,  5, 10, -20, 100 },
};

static int flips(int p, int r, int c)
{
    if (board[r][c]) return 0;
    int opp = 3 - p, total = 0;
    for (int d = 0; d < 8; d++) {
        int rr = r + DR[d], cc = c + DC[d], cnt = 0;
        while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && board[rr][cc] == opp) {
            rr += DR[d]; cc += DC[d]; cnt++;
        }
        if (cnt && rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && board[rr][cc] == p)
            total += cnt;
    }
    return total;
}

static void apply(int p, int r, int c)
{
    int opp = 3 - p;
    board[r][c] = p;
    for (int d = 0; d < 8; d++) {
        int rr = r + DR[d], cc = c + DC[d], cnt = 0;
        while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && board[rr][cc] == opp) {
            rr += DR[d]; cc += DC[d]; cnt++;
        }
        if (cnt && rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && board[rr][cc] == p) {
            rr = r + DR[d]; cc = c + DC[d];
            for (int k = 0; k < cnt; k++) { board[rr][cc] = p; rr += DR[d]; cc += DC[d]; }
        }
    }
}

static int has_move(int p)
{
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (flips(p, r, c)) return 1;
    return 0;
}

static void ai_move(void)
{
    int bestv = -100000, br = -1, bc = -1, bf = -1;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int f = flips(2, r, c);
            if (!f) continue;
            if (WT[r][c] > bestv || (WT[r][c] == bestv && f > bf)) {
                bestv = WT[r][c]; bf = f; br = r; bc = c;
            }
        }
    if (br >= 0) apply(2, br, bc);
}

static void run(void)
{
    for (;;) {
        if (!has_move(1) && !has_move(2)) { over = 1; return; }
        if (!has_move(turn)) { turn = 3 - turn; continue; }
        if (turn == 1) return;
        ai_move();
        turn = 1;
    }
}

static void reset(int inst)
{
    (void)inst;
    api->memset(board, 0, sizeof board);
    board[3][3] = board[4][4] = 2;
    board[3][4] = board[4][3] = 1;
    turn = 1;
    over = 0;
    run();
}

static int score(int p)
{
    int n = 0;
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (board[r][c] == p) n++;
    return n;
}

static void disc(int x, int y, u8 col)
{
    int cx = x + CELL / 2, cy = y + CELL / 2;
    api->fill_circle(cx, cy, CELL / 2 - 4, col);
    api->circle(cx, cy, CELL / 2 - 4, C_G0 + 1);
    if (col == C_WHITE) api->fill_circle(cx - 3, cy - 3, 2, C_G0 + 7);
}

static void rv_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)ch;
    int bs = score(1), ws = score(2);

    if (gfx) { gfx->set_dither(1); gfx->fill_gradient(cx, cy, cw, HDR, GRGB(214, 218, 228), GRGB(180, 186, 202), 1); gfx->set_dither(0); } else api->fill_rect(cx, cy, cw, HDR, C_FACE);
    int hov = api->win_is_hovered(w) && *api->mouse_x >= cx + M && *api->mouse_x < cx + M + 44 &&
              *api->mouse_y >= cy + 4 && *api->mouse_y < cy + 24;
    api->panel(cx + M, cy + 4, 44, 20, hov);
    api->draw_text(cx + M + 8, cy + 8, "New", C_BLACK);
    char b[40];

    api->kfmt(b, sizeof b, "You %d-%d AI", bs, ws);
    api->draw_text(cx + M + 60, cy + 8, b, C_NAVY);
    const char *msg;
    if (over) msg = bs > ws ? "you win!" : ws > bs ? "AI wins" : "a tie";
    else      msg = "your move";
    api->draw_text(cx + cw - (int)api->strlen(msg) * 8 - M, cy + 8, msg,
                   over ? C_MAROON : C_G0 + 3);

    int bw = 8 * CELL;
    api->fill_rect(cx + OX, cy + OY, bw, bw, C_GREEN);
    for (int i = 0; i <= 8; i++) {
        api->hline(cx + OX, cy + OY + i * CELL, bw + 1, C_BLACK);
        api->vline(cx + OX + i * CELL, cy + OY, bw + 1, C_BLACK);
    }
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int x = cx + OX + c * CELL, y = cy + OY + r * CELL;
            if (board[r][c]) disc(x, y, board[r][c] == 1 ? C_BLACK : C_WHITE);
            else if (!over && turn == 1 && flips(1, r, c))
                api->fill_circle(x + CELL / 2, y + CELL / 2, 3, C_BGREEN);
        }
}

static void rv_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    if (ly >= 4 && ly < 24 && lx >= M && lx < M + 44) { reset(0); return; }
    if (over || turn != 1) return;
    int c = (lx - OX) / CELL, r = (ly - OY) / CELL;
    if (lx < OX || ly < OY || r < 0 || r >= 8 || c < 0 || c >= 8) return;
    if (flips(1, r, c)) {
        apply(1, r, c);
        turn = 2;
        run();
    }
}

static void rv_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 2 * M + 8 * CELL;
    *h = HDR + 8 * CELL + M;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Reversi"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Reversi", .max_inst = 1, .in_menu = 1,
        .open = reset, .draw = rv_draw, .mouse = rv_mouse,
        .client_size = rv_csize,
    };
    return k->register_app(&d) < 0;
}
