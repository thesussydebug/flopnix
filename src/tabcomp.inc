/* Completes shell commands and file names. */
#pragma once

static inline int tc_starts(const char *s, const char *pre)
{
    int i = 0;
    while (pre[i]) {
        if (s[i] != pre[i]) return 0;
        i++;
    }
    return 1;
}

static inline int tc_count(const char *prefix, const char *const *cands, int n)
{
    if (!prefix || !cands) return 0;
    int c = 0;
    for (int i = 0; i < n; i++)
        if (cands[i] && tc_starts(cands[i], prefix)) c++;
    return c;
}

static inline const char *tc_nth(const char *prefix, const char *const *cands,
                                 int n, int i)
{
    if (!prefix || !cands || i < 0) return 0;
    for (int k = 0; k < n; k++)
        if (cands[k] && tc_starts(cands[k], prefix) && i-- == 0) return cands[k];
    return 0;
}

static inline int tc_common(const char *prefix, const char *const *cands, int n,
                            char *out, int cap)
{
    int m = tc_count(prefix, cands, n);
    if (!m || !out || cap < 1) return m;

    const char *first = tc_nth(prefix, cands, n, 0);
    int len = 0;
    while (first[len]) len++;
    for (int k = 1; k < m; k++) {
        const char *c = tc_nth(prefix, cands, n, k);
        int j = 0;
        while (j < len && c[j] && first[j] == c[j]) j++;
        len = j;
    }
    if (len > cap - 1) len = cap - 1;
    for (int i = 0; i < len; i++) out[i] = first[i];
    out[len] = 0;
    return m;
}
