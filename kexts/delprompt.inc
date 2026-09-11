#define DP_TAIL " This cannot be undone."

static inline int dp_put(char *out, int cap, int at, const char *s)
{
    for (; *s && at < cap - 1; s++) out[at++] = *s;
    out[at] = 0;
    return at;
}

static inline int dp_num(char *out, int cap, int at, int v)
{
    char t[12];
    int n = 0;
    if (v < 0) v = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n-- && at < cap - 1) out[at++] = t[n];
    out[at] = 0;
    return at;
}

static inline void del_prompt(char *out, int cap, int nfiles, int nfolders,
                              const char *first)
{
    if (cap <= 0) return;
    out[0] = 0;
    int total = nfiles + nfolders;
    int at = 0;

    if (total == 1 && first && first[0]) {
        at = dp_put(out, cap, at, nfolders ? "Delete folder \"" : "Delete \"");
        at = dp_put(out, cap, at, first);
        at = dp_put(out, cap, at, "\"?");
    } else {
        at = dp_put(out, cap, at, "Delete ");
        at = dp_num(out, cap, at, total);

        at = dp_put(out, cap, at, nfolders ? " items?" : " files?");
    }
    dp_put(out, cap, at, DP_TAIL);
}
