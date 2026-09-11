/* Moves pieces, checks collisions, and clears rows. */
#define TET_W 10
#define TET_H 20

enum { TP_I, TP_O, TP_T, TP_S, TP_Z, TP_J, TP_L };

static const u16 tet_mask[7][4] = {
    { 0x00F0, 0x4444, 0x0F00, 0x2222 },
    { 0x0066, 0x0066, 0x0066, 0x0066 },
    { 0x0072, 0x0262, 0x0270, 0x0232 },
    { 0x0036, 0x0462, 0x0036, 0x0462 },
    { 0x0063, 0x0264, 0x0063, 0x0264 },
    { 0x0071, 0x0226, 0x0470, 0x0322 },
    { 0x0074, 0x0622, 0x0170, 0x0223 },
};

static inline int tet_fits(const u8 *bd, int piece, int rot, int px, int py)
{
    u16 m = tet_mask[piece][rot & 3];
    for (int cy = 0; cy < 4; cy++)
        for (int cx = 0; cx < 4; cx++) {
            if (!(m & (1 << (cy * 4 + cx)))) continue;
            int x = px + cx, y = py + cy;
            if (x < 0 || x >= TET_W || y < 0 || y >= TET_H) return 0;
            if (bd[y * TET_W + x]) return 0;
        }
    return 1;
}

static inline void tet_lock(u8 *bd, int piece, int rot, int px, int py)
{
    u16 m = tet_mask[piece][rot & 3];
    for (int cy = 0; cy < 4; cy++)
        for (int cx = 0; cx < 4; cx++) {
            if (!(m & (1 << (cy * 4 + cx)))) continue;
            int x = px + cx, y = py + cy;
            if (x >= 0 && x < TET_W && y >= 0 && y < TET_H)
                bd[y * TET_W + x] = (u8)(piece + 1);
        }
}

static inline int tet_clear(u8 *bd)
{
    int cleared = 0;
    for (int y = TET_H - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TET_W; x++)
            if (!bd[y * TET_W + x]) { full = 0; break; }
        if (!full) continue;
        cleared++;
        for (int yy = y; yy > 0; yy--)
            for (int x = 0; x < TET_W; x++)
                bd[yy * TET_W + x] = bd[(yy - 1) * TET_W + x];
        for (int x = 0; x < TET_W; x++) bd[x] = 0;
        y++;
    }
    return cleared;
}

static inline int tet_score(int lines, int level)
{
    static const int base[5] = { 0, 40, 100, 300, 1200 };
    if (lines < 0 || lines > 4) return 0;
    return base[lines] * (level + 1);
}

static inline int tet_level(int total_lines)
{
    return total_lines / 10;
}

static inline int tet_speed(int level)
{
    int t = 24 - level * 3;
    return t < 2 ? 2 : t;
}

static inline int tet_drop_y(const u8 *bd, int piece, int rot, int px, int py)
{
    while (tet_fits(bd, piece, rot, px, py + 1)) py++;
    return py;
}
