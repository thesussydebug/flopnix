/* Places mines and updates Minesweeper board state. */
#pragma once

#define MN_MAXW 30
#define MN_MAXH 16
#define MN_MAXC (MN_MAXW * MN_MAXH)

enum { MN_NONE = 0, MN_FLAG = 1, MN_QUERY = 2 };

typedef struct {
    int w, h, mines;
    unsigned char mine[MN_MAXC];
    unsigned char adj[MN_MAXC];
    unsigned char shown[MN_MAXC];
    unsigned char mark[MN_MAXC];
} MnBoard;

static inline int mn_i(const MnBoard *b, int r, int c) { return r * b->w + c; }
static inline int mn_in(const MnBoard *b, int r, int c)
{
    return r >= 0 && r < b->h && c >= 0 && c < b->w;
}

static inline void mn_reset(MnBoard *b, int w, int h, int mines)
{
    if (w < 1) w = 1;
    if (w > MN_MAXW) w = MN_MAXW;
    if (h < 1) h = 1;
    if (h > MN_MAXH) h = MN_MAXH;
    b->w = w;
    b->h = h;

    int cap = w * h - (w < 3 ? w : 3) * (h < 3 ? h : 3);
    if (mines > cap) mines = cap;
    if (mines < 0) mines = 0;
    b->mines = mines;
    for (int i = 0; i < w * h; i++) {
        b->mine[i] = b->adj[i] = b->shown[i] = b->mark[i] = 0;
    }
}

static inline void mn_place(MnBoard *b, int sr, int sc, unsigned (*rng)(void))
{
    if (!mn_in(b, sr, sc)) return;
    int eligible = 0;
    for (int r = 0; r < b->h; r++)
        for (int c = 0; c < b->w; c++) {
            b->mine[mn_i(b, r, c)] = 0;
            if (r < sr - 1 || r > sr + 1 || c < sc - 1 || c > sc + 1)
                eligible++;
        }
    int left = b->mines;

    for (int r = 0; r < b->h; r++)
        for (int c = 0; c < b->w; c++) {
            if (r >= sr - 1 && r <= sr + 1 && c >= sc - 1 && c <= sc + 1)
                continue;
            if (left && rng() % (unsigned)eligible < (unsigned)left) {
                b->mine[mn_i(b, r, c)] = 1;
                left--;
            }
            eligible--;
        }
    for (int r = 0; r < b->h; r++)
        for (int c = 0; c < b->w; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if ((dr || dc) && mn_in(b, r + dr, c + dc) &&
                        b->mine[mn_i(b, r + dr, c + dc)]) n++;
            b->adj[mn_i(b, r, c)] = (unsigned char)n;
        }
}

static inline int mn_reveal(MnBoard *b, int r, int c)
{
    if (!mn_in(b, r, c)) return 0;
    int start = mn_i(b, r, c);
    if (b->shown[start] || b->mark[start] == MN_FLAG) return 0;
    static short stk[MN_MAXC];
    int sp = 0, opened = 0;
    stk[sp++] = (short)start;
    while (sp > 0) {
        int i = stk[--sp];
        if (b->shown[i] || b->mark[i] == MN_FLAG) continue;
        b->shown[i] = 1;
        b->mark[i] = MN_NONE;
        opened++;
        if (b->adj[i] || b->mine[i]) continue;
        int rr = i / b->w, cc = i % b->w;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                if (!dr && !dc) continue;
                if (!mn_in(b, rr + dr, cc + dc)) continue;
                int j = mn_i(b, rr + dr, cc + dc);
                if (!b->shown[j] && b->mark[j] != MN_FLAG && sp < MN_MAXC)
                    stk[sp++] = (short)j;
            }
    }
    return opened;
}

static inline int mn_flags_around(const MnBoard *b, int r, int c)
{
    int n = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if ((dr || dc) && mn_in(b, r + dr, c + dc) &&
                b->mark[mn_i(b, r + dr, c + dc)] == MN_FLAG) n++;
    return n;
}

static inline int mn_chord(MnBoard *b, int r, int c, int *hit)
{
    if (hit) *hit = 0;
    if (!mn_in(b, r, c)) return 0;
    int i = mn_i(b, r, c);
    if (!b->shown[i] || !b->adj[i]) return 0;
    if (mn_flags_around(b, r, c) != b->adj[i]) return 0;
    int opened = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            if (!dr && !dc) continue;
            if (!mn_in(b, r + dr, c + dc)) continue;
            int j = mn_i(b, r + dr, c + dc);
            if (b->shown[j] || b->mark[j] == MN_FLAG) continue;
            if (b->mine[j]) {
                b->shown[j] = 1;
                opened++;
                if (hit) *hit = 1;
            } else opened += mn_reveal(b, r + dr, c + dc);
        }
    return opened;
}

static inline void mn_cycle_mark(MnBoard *b, int r, int c)
{
    if (!mn_in(b, r, c)) return;
    int i = mn_i(b, r, c);
    if (b->shown[i]) return;
    b->mark[i] = (unsigned char)((b->mark[i] + 1) % 3);
}

static inline int mn_count_mark(const MnBoard *b, int kind)
{
    int n = 0;
    for (int i = 0; i < b->w * b->h; i++) if (b->mark[i] == kind) n++;
    return n;
}

static inline int mn_count_shown(const MnBoard *b)
{
    int n = 0;
    for (int i = 0; i < b->w * b->h; i++) if (b->shown[i]) n++;
    return n;
}

static inline int mn_won(const MnBoard *b)
{
    for (int i = 0; i < b->w * b->h; i++)
        if (!b->mine[i] && !b->shown[i]) return 0;
    return 1;
}
