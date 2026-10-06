#pragma once
#define BAK_MAX 999

static inline int bak_name(int n, char *out, int cap)
{
    static const char head[] = "/FLOPNIX_backup", tail[] = ".img";
    char digits[4];
    int d = 0, len = 0;
    if (n < 1 || n > BAK_MAX || cap < 23) return 0;
    for (int v = n; v; v /= 10) digits[d++] = (char)('0' + v % 10);
    for (const char *p = head; *p; p++) out[len++] = *p;
    while (d) out[len++] = digits[--d];
    for (const char *p = tail; *p; p++) out[len++] = *p;
    out[len] = 0;
    return len;
}

static inline int bak_next(int (*exists)(const char *path, void *ctx), void *ctx,
                           char *out, int cap)
{
    for (int n = 1; n <= BAK_MAX; n++) {
        if (!bak_name(n, out, cap)) return 0;
        if (!exists(out, ctx)) return n;
    }
    return 0;
}
