/* Finds an unused screenshot file name. */
#pragma once

static inline int shot_next(int (*exists)(const char *name, void *ctx),
                            void *ctx, char *out, int cap)
{
    if (cap < 11) return 0;
    for (int n = 1; n <= 99; n++) {
        out[0] = 'S'; out[1] = 'H'; out[2] = 'O'; out[3] = 'T';
        out[4] = (char)('0' + n / 10);
        out[5] = (char)('0' + n % 10);
        out[6] = '.'; out[7] = 'B'; out[8] = 'M'; out[9] = 'P';
        out[10] = 0;
        if (!exists(out, ctx)) return 1;
    }
    return 0;
}
