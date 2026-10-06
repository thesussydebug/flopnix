#pragma once

#define PW_WIN   32768u
#define PW_HASH  4096u
#define PW_CHAIN 32
#define PW_WORK  (PW_HASH * 4 + PW_WIN * 2 + 256)

typedef struct {
    u8  *p;
    u32  len, lim;
    u32  acc;
    int  nb;
} PwOut;

static const u16 pw_lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 pw_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 pw_dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const u8 pw_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static inline void pw_byte(PwOut *o, u32 v)
{
    if (o->len < o->lim) o->p[o->len] = (u8)v;
    o->len++;
}

static inline void pw_bits(PwOut *o, u32 v, int n)
{
    o->acc |= v << o->nb;
    o->nb += n;
    while (o->nb >= 8) { pw_byte(o, o->acc); o->acc >>= 8; o->nb -= 8; }
}

static inline void pw_be32(PwOut *o, u32 v)
{
    pw_byte(o, v >> 24); pw_byte(o, v >> 16); pw_byte(o, v >> 8); pw_byte(o, v);
}

static inline u32 pw_open(PwOut *o, const char *type)
{
    pw_be32(o, 0);
    u32 at = o->len;
    for (int i = 0; i < 4; i++) pw_byte(o, (u8)type[i]);
    return at;
}

static inline void pw_close(PwOut *o, u32 at, u32 (*crc)(const void *, u32))
{
    if (o->len > o->lim) return;
    u32 n = o->len - at - 4;
    u8 *l = o->p + at - 4;
    l[0] = (u8)(n >> 24); l[1] = (u8)(n >> 16); l[2] = (u8)(n >> 8); l[3] = (u8)n;
    pw_be32(o, crc(o->p + at, n + 4));
}

static inline void pw_sym(PwOut *o, const u8 *rev, u32 s)
{
    if (s < 144)      pw_bits(o, rev[0x30 + s], 8);
    else if (s < 256) pw_bits(o, (u32)rev[(0x190 + s - 144) & 0xFF] << 1 | 1, 9);
    else if (s < 280) pw_bits(o, rev[s - 256] >> 1, 7);
    else              pw_bits(o, rev[0xC0 + s - 280], 8);
}

static inline void pw_match(PwOut *o, const u8 *rev, u32 len, u32 dist)
{
    int c = 28;
    while (pw_lbase[c] > len) c--;
    pw_sym(o, rev, 257 + (u32)c);
    pw_bits(o, len - pw_lbase[c], pw_lext[c]);
    c = 29;
    while (pw_dbase[c] > dist) c--;
    pw_bits(o, rev[c] >> 3, 5);
    pw_bits(o, dist - pw_dbase[c], pw_dext[c]);
}

static inline u32 pw_hash(const u8 *p)
{
    return ((u32)p[0] << 8 ^ (u32)p[1] << 4 ^ p[2]) * 2654435761u >> 20;
}

static inline u32 pngw_encode(u8 *out, u32 cap, const u8 *raw, int w, int h,
                              const u8 *pal, u32 (*crc)(const void *, u32),
                              void *work)
{
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    u32 n = (u32)h * (u32)(w + 1);
    u32 *head = (u32 *)work;
    u16 *prev = (u16 *)(head + PW_HASH);
    u8  *rev  = (u8 *)(prev + PW_WIN);
    PwOut o;
    o.p = out; o.len = 0; o.lim = cap; o.acc = 0; o.nb = 0;

    u32 a = 1, b = 0, top = 0;
    for (u32 i = 0; i < n; ) {
        u32 k = n - i > 5552 ? 5552 : n - i;
        while (k--) {
            u32 v = raw[i++];
            if (v > top) top = v;
            a += v; b += a;
        }
        a %= 65521; b %= 65521;
    }

    for (int i = 0; i < 8; i++) pw_byte(&o, sig[i]);
    u32 at = pw_open(&o, "IHDR");
    pw_be32(&o, (u32)w); pw_be32(&o, (u32)h);
    pw_byte(&o, 8); pw_byte(&o, 3);
    pw_byte(&o, 0); pw_byte(&o, 0); pw_byte(&o, 0);
    pw_close(&o, at, crc);
    at = pw_open(&o, "PLTE");
    for (u32 i = 0; i < (top + 1) * 3; i++) pw_byte(&o, pal[i]);
    pw_close(&o, at, crc);

    for (u32 i = 0; i < PW_HASH; i++) head[i] = 0;
    for (u32 i = 0; i < 256; i++) {
        u32 r = 0;
        for (int k = 0; k < 8; k++) r |= (i >> k & 1) << (7 - k);
        rev[i] = (u8)r;
    }

    at = pw_open(&o, "IDAT");
    pw_byte(&o, 0x78); pw_byte(&o, 0x01);
    pw_bits(&o, 3, 3);
    u32 i = 0;
    while (i < n && o.len <= o.lim) {
        u32 best = 0, dist = 0;
        if (i + 3 <= n) {
            u32 maxl = n - i > 258 ? 258 : n - i;
            u32 cand = head[pw_hash(raw + i)];
            for (int depth = 0; cand && depth < PW_CHAIN; depth++) {
                u32 p = cand - 1, l = 0;
                if (i - p >= PW_WIN) break;
                if (raw[p + best] == raw[i + best]) {
                    while (l < maxl && raw[p + l] == raw[i + l]) l++;
                    if (l > best) { best = l; dist = i - p; }
                    if (best == maxl) break;
                }
                u32 d = prev[p & (PW_WIN - 1)];
                if (!d) break;
                cand = p - d + 1;
            }
        }
        if (best < 3 || (best == 3 && dist > 4096)) best = 0;
        if (best) pw_match(&o, rev, best, dist);
        else { pw_sym(&o, rev, raw[i]); best = 1; }
        for (; best; best--, i++) {
            if (i + 3 > n) continue;
            u32 hh = pw_hash(raw + i), d = head[hh] ? i + 1 - head[hh] : 0;
            prev[i & (PW_WIN - 1)] = (u16)(d < PW_WIN ? d : 0);
            head[hh] = i + 1;
        }
    }
    pw_sym(&o, rev, 256);
    if (o.nb) pw_bits(&o, 0, 8 - o.nb);
    pw_be32(&o, b << 16 | a);
    pw_close(&o, at, crc);
    at = pw_open(&o, "IEND");
    pw_close(&o, at, crc);
    return o.len <= o.lim ? o.len : 0;
}
