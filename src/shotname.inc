/* Finds an unused screenshot file name. */
#pragma once

static inline int shot_next(int (*exists)(const char *name, void *ctx),
                            void *ctx, const char *ext, char *out, int cap)
{
    if (cap < 11) return 0;
    for (int n = 1; n <= 99; n++) {
        out[0] = 'S'; out[1] = 'H'; out[2] = 'O'; out[3] = 'T';
        out[4] = (char)('0' + n / 10);
        out[5] = (char)('0' + n % 10);
        out[6] = '.'; out[10] = 0;
        int taken = 0;
        for (int k = 0; k < 2 && !taken; k++) {
            const char *e = k ? "PNG" : "BMP";
            out[7] = e[0]; out[8] = e[1]; out[9] = e[2];
            taken = exists(out, ctx);
        }
        if (taken) continue;
        out[7] = ext[0]; out[8] = ext[1]; out[9] = ext[2];
        return 1;
    }
    return 0;
}
