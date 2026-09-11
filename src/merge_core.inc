/* Moves and merges the 2048 board. */
#pragma once

static u32 mg_row(u8 r[4])
{
    u8 out[4] = {0}; int used = 0, merged = -1; u32 score = 0;
    for (int i = 0; i < 4; i++) if (r[i]) {
        if (used && merged != used-1 && out[used-1] == r[i] && r[i] < 30) {
            out[used-1]++; score += 1u << out[used-1]; merged = used-1;
        } else out[used++] = r[i];
    }
    for (int i = 0; i < 4; i++) r[i] = out[i];
    return score;
}
static void mg_rotate(u8 b[16])
{
    u8 tmp[16];
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) tmp[x*4+3-y] = b[y*4+x];
    for (int i = 0; i < 16; i++) b[i] = tmp[i];
}

static int mg_move(u8 b[16], int dir, u32 *score)
{
    u8 old[16]; int changed = 0; *score = 0;
    for (int i = 0; i < 16; i++) old[i] = b[i];
    for (int i = 0; i < dir; i++) mg_rotate(b);
    for (int i = 0; i < 4; i++) {
        u32 gain=mg_row(b+i*4);
        *score=*score>0xffffffffu-gain?0xffffffffu:*score+gain;
    }
    for (int i = dir; i < 4; i++) mg_rotate(b);
    for (int i = 0; i < 16; i++) if (old[i] != b[i]) changed = 1;
    return changed;
}
static int mg_available(const u8 b[16])
{
    for (int i = 0; i < 16; i++) {
        if (!b[i]) return 1;
        if (b[i] < 30 && ((i%4 < 3 && b[i] == b[i+1]) || (i < 12 && b[i] == b[i+4]))) return 1;
    }
    return 0;
}
