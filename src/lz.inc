/* Compresses and expands LZSS data. */
#pragma once

#define LZ_WIN    4096
#define LZ_MINM   3
#define LZ_MAXM   18
#define LZ_HDR    12
#define LZ_HASHN  1024
#define LZ_CHAIN  16

static inline u16 lz_csum(const u8 *p, u32 n)
{
    u16 s = 0;
    for (u32 i = 0; i < n; i++)
        s = (u16)(((s << 1) | (s >> 15)) + p[i]);
    return s;
}

static inline void lz_put32(u8 *d, u32 v)
{
    d[0] = (u8)v; d[1] = (u8)(v >> 8); d[2] = (u8)(v >> 16); d[3] = (u8)(v >> 24);
}
static inline u32 lz_get32(const u8 *d)
{
    return (u32)d[0] | ((u32)d[1] << 8) | ((u32)d[2] << 16) | ((u32)d[3] << 24);
}

static inline void lz_head(u8 *d, u32 orig, u16 csum, int stored)
{
    d[0] = 'P'; d[1] = 'Z'; d[2] = '1'; d[3] = 0;
    lz_put32(d + 4, orig);
    d[8] = (u8)csum; d[9] = (u8)(csum >> 8);
    d[10] = (u8)stored;
    d[11] = 0;
}

static inline int lz_hash3(const u8 *p)
{
    return (int)(((u32)p[0] << 8 ^ (u32)p[1] << 4 ^ (u32)p[2]) & (LZ_HASHN - 1));
}

static inline u32 lz_pack(const u8 *src, u32 n, u8 *dst, u32 dcap)
{
    static u16 head[LZ_HASHN], prev[LZ_WIN];
    u16 csum = lz_csum(src, n);
    if (dcap < LZ_HDR) return 0;
    if (n == 0) { lz_head(dst, 0, csum, 0); return LZ_HDR; }

    for (int i = 0; i < LZ_HASHN; i++) head[i] = 0xFFFF;

    u32 o = LZ_HDR;
    u32 i = 0;
    while (i < n) {
        u32 flag_at = o;
        if (o >= dcap) goto stored;
        dst[o++] = 0;
        u8 flags = 0;
        for (int bit = 0; bit < 8 && i < n; bit++) {
            u32 best = 0, bestoff = 0;
            if (i + LZ_MINM <= n) {
                int h = lz_hash3(src + i);
                u32 cand = head[h];
                for (int depth = 0; depth < LZ_CHAIN && cand != 0xFFFF; depth++) {
                    u32 pos = i - ((i - cand) & (LZ_WIN - 1));
                    if (pos >= i || i - pos > LZ_WIN) break;
                    u32 len = 0, cap = n - i;
                    if (cap > LZ_MAXM) cap = LZ_MAXM;
                    while (len < cap && src[pos + len] == src[i + len]) len++;
                    if (len > best) { best = len; bestoff = i - pos; }
                    if (best == LZ_MAXM) break;
                    cand = prev[cand & (LZ_WIN - 1)];
                }
            }
            if (best >= LZ_MINM) {
                if (o + 2 > dcap) goto stored;
                u32 e = bestoff - 1;
                dst[o++] = (u8)e;
                dst[o++] = (u8)(((e >> 8) << 4) | (best - LZ_MINM));
                for (u32 k = 0; k < best; k++) {
                    if (i + k + LZ_MINM <= n) {
                        int h2 = lz_hash3(src + i + k);
                        prev[(i + k) & (LZ_WIN - 1)] = head[h2];
                        head[h2] = (u16)((i + k) & 0xFFFF);
                    }
                }
                i += best;
            } else {
                if (o + 1 > dcap) goto stored;
                flags |= (u8)(1u << bit);
                if (i + LZ_MINM <= n) {
                    int h2 = lz_hash3(src + i);
                    prev[i & (LZ_WIN - 1)] = head[h2];
                    head[h2] = (u16)(i & 0xFFFF);
                }
                dst[o++] = src[i++];
            }
        }
        dst[flag_at] = flags;
        if (o >= LZ_HDR + n) goto stored;
    }
    lz_head(dst, n, csum, 0);
    return o;

stored:
    if (dcap < LZ_HDR + n) return 0;
    lz_head(dst, n, csum, 1);
    for (u32 k = 0; k < n; k++) dst[LZ_HDR + k] = src[k];
    return LZ_HDR + n;
}

static inline int lz_unpack(const u8 *src, u32 n, u8 *dst, u32 dcap)
{
    if (n < LZ_HDR) return -1;
    if (src[0] != 'P' || src[1] != 'Z' || src[2] != '1' || src[3] != 0) return -1;
    if (src[10] > 1 || src[11]) return -1;
    u32 orig = lz_get32(src + 4);
    u16 want = (u16)(src[8] | (src[9] << 8));
    int stored = src[10];
    if (orig > dcap) return -1;

    u32 o = 0, i = LZ_HDR;
    if (stored) {
        if (n - LZ_HDR != orig) return -1;
        for (; o < orig; o++) dst[o] = src[LZ_HDR + o];
    } else {
        while (o < orig) {
            if (i >= n) return -1;
            u8 flags = src[i++];
            for (int bit = 0; bit < 8 && o < orig; bit++) {
                if (flags & (1u << bit)) {
                    if (i >= n) return -1;
                    dst[o++] = src[i++];
                } else {
                    if (i + 1 >= n) return -1;
                    u32 b0 = src[i++], b1 = src[i++];
                    u32 off = (((b1 >> 4) << 8) | b0) + 1;
                    u32 len = (b1 & 0x0F) + LZ_MINM;
                    if (off > o) return -1;
                    if (o + len > orig) return -1;
                    for (u32 k = 0; k < len; k++, o++) dst[o] = dst[o - off];
                }
            }
        }
    }
    if (!stored && i != n) return -1;
    if (lz_csum(dst, orig) != want) return -1;
    return (int)orig;
}
