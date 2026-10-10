static inline int gzn_tail(const char *s, int n, const char *tail)
{
    int t = 0;
    while (tail[t]) t++;
    if (n < t) return 0;
    for (int i = 0; i < t; i++) {
        int c = s[n - t + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != tail[i]) return 0;
    }
    return t;
}

static inline int gz_out_name(const char *in, char *out, int cap)
{
    int n = 0;
    while (in[n]) n++;
    int tar = gzn_tail(in, n, ".tgz"), cut = tar ? tar : gzn_tail(in, n, ".gz");
    int keep = n - cut;
    if (!cut || keep <= 0 || in[keep - 1] == '/' || in[keep - 1] == ':') return 0;
    if (keep + (tar ? 4 : 0) >= cap) return 0;
    for (int i = 0; i < keep; i++) out[i] = in[i];
    if (tar) { out[keep++] = '.'; out[keep++] = 't'; out[keep++] = 'a'; out[keep++] = 'r'; }
    out[keep] = 0;
    return 1;
}

static inline int gz_is(const u8 *d, u32 n)
{
    return n >= 18 && d[0] == 0x1F && d[1] == 0x8B && d[2] == 8;
}

static inline int gz_member(const u8 *d, u32 n, char *out, int cap)
{
    if (!gz_is(d, n) || !(d[3] & 8) || cap < 2) return 0;
    u32 at = 10;
    if (d[3] & 4) {
        u32 extra = (u32)d[10] | (u32)d[11] << 8;
        if (extra > n - 12) return 0;
        at = 12 + extra;
    }
    int k = 0;
    for (; at < n && d[at]; at++) {
        int c = d[at];
        if (c == '/' || c == '\\' || c == ':') { k = 0; continue; }
        if (c < 32 || c > 126) c = '_';
        if (k < cap - 1) out[k++] = (char)c;
    }
    out[k] = 0;
    return at < n && k > 0;
}
