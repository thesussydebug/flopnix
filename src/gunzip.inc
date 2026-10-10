#pragma once
#define GZ_WIN 32768u
enum { GZ_STOP = -2, GZ_ERR = -1, GZ_MORE = 0, GZ_DONE = 1 };
enum { GZS_HEAD, GZS_XLEN, GZS_SKIP, GZS_SKIPZ, GZS_BLOCK, GZS_STORED, GZS_RAW, GZS_DYN,
       GZS_CLEN, GZS_LENS, GZS_REP, GZS_SYM, GZS_LEXT, GZS_DIST, GZS_DEXT, GZS_CRC,
       GZS_SIZE, GZS_END };

typedef struct { u16 count[16], sym[288]; } GzHuff;
typedef struct {
    u32 bb, crc, total, left, wpos, wout, omask, room, ct[16];
    int nb, state, last, sym, len, idx, nlen, ndist, ncode, flags;
    u8 *o;
    GzHuff lit, dst;
    u8 lens[320];
} Gunzip;

static void gz_init(Gunzip *g, u8 *win)
{
    u8 *p = (u8 *)g;
    for (u32 i = 0; i < sizeof *g; i++) p[i] = 0;
    for (u32 i = 0; i < 16; i++) {
        u32 c = i;
        for (int k = 0; k < 4; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        g->ct[i] = c;
    }
    g->o = win;
    g->omask = GZ_WIN - 1;
}

static inline int gz_done(const Gunzip *g) { return g->state == GZS_END; }

static int gz_flush(Gunzip *g, int (*sink)(const u8 *, int, void *), void *ctx)
{
    while (g->wout != g->wpos) {
        u32 at = g->wout & g->omask, n = g->wpos - g->wout, c = ~g->crc;
        if (n - 1 > g->omask - at) n = g->omask - at + 1;
        for (u32 i = 0; i < n; i++) {
            c ^= g->o[at + i];
            c = (c >> 4) ^ g->ct[c & 15];
            c = (c >> 4) ^ g->ct[c & 15];
        }
        g->crc = ~c;
        g->wout += n;
        if (sink && !sink(g->o + at, (int)n, ctx)) return 0;
    }
    return 1;
}

static int gz_put(Gunzip *g, u8 v, int (*sink)(const u8 *, int, void *), void *ctx)
{
    if (g->room && g->wpos >= g->room) return 0;
    g->o[g->wpos++ & g->omask] = v;
    g->total++;
    return g->room || g->wpos - g->wout < GZ_WIN || gz_flush(g, sink, ctx);
}

static int gz_bits(Gunzip *g, const u8 **in, const u8 *end, int n)
{
    while (g->nb < n) {
        if (*in == end) return 0;
        g->bb |= (u32)*(*in)++ << g->nb;
        g->nb += 8;
    }
    return 1;
}

static u32 gz_take(Gunzip *g, int n)
{
    u32 v = n < 32 ? g->bb & ((1u << n) - 1) : g->bb;
    g->bb = n < 32 ? g->bb >> n : 0;
    g->nb -= n;
    return v;
}

static int gz_sym(Gunzip *g, const GzHuff *h, const u8 **in, const u8 *end)
{
    for (;;) {
        int code = 0, first = 0, index = 0, len;
        u32 bits = g->bb;
        for (len = 1; len <= 15 && len <= g->nb; len++) {
            code |= (int)(bits & 1);
            bits >>= 1;
            int count = h->count[len];
            if (code - count < first) {
                gz_take(g, len);
                return h->sym[index + (code - first)];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        if (len > 15) return -2;
        if (*in == end) return -1;
        g->bb |= (u32)*(*in)++ << g->nb;
        g->nb += 8;
    }
}

static int gz_build(GzHuff *h, const u8 *len, int n)
{
    u16 offs[16];
    int left = 1;
    for (int i = 0; i < 16; i++) h->count[i] = 0;
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    for (int i = 1; i < 16; i++) {
        left = (left << 1) - h->count[i];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->sym[offs[len[i]]++] = (u16)i;
    return left;
}

static void gz_head_next(Gunzip *g)
{
    g->idx = 0;
    g->left = 0;
    if (g->flags & 4) { g->flags &= ~4; g->state = GZS_XLEN; }
    else if (g->flags & 24) { g->flags &= g->flags & 8 ? ~8 : ~16; g->state = GZS_SKIPZ; }
    else if (g->flags & 2) { g->flags = 0; g->left = 2; g->state = GZS_SKIP; }
    else g->state = GZS_BLOCK;
}

static int gz_feed(Gunzip *g, const u8 *in, u32 n, int (*sink)(const u8 *, int, void *), void *ctx)
{
    static const u16 lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35,
        43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const u8 lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const u16 dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const u8 dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static const u8 order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const u8 *end = in + n;
    int s, r;
    u32 v;

    if (g->state < 0) return g->state;
    for (;;) {
        switch (g->state) {
        case GZS_HEAD:
            while (g->idx < 10) {
                if (in == end) goto more;
                g->lens[g->idx++] = *in++;
            }
            if (g->lens[0] != 0x1F || g->lens[1] != 0x8B || g->lens[2] != 8 || (g->lens[3] & 0xE0)) goto bad;
            g->flags = g->lens[3];
            g->crc = g->total = 0;
            gz_head_next(g);
            break;
        case GZS_XLEN:
            while (g->idx < 2) {
                if (in == end) goto more;
                g->left |= (u32)*in++ << (8 * g->idx++);
            }
            g->state = GZS_SKIP;
            break;
        case GZS_SKIP:
            while (g->left) {
                if (in == end) goto more;
                in++;
                g->left--;
            }
            gz_head_next(g);
            break;
        case GZS_SKIPZ:
            do {
                if (in == end) goto more;
            } while (*in++);
            gz_head_next(g);
            break;
        case GZS_BLOCK:
            if (!gz_bits(g, &in, end, 3)) goto more;
            g->last = (int)gz_take(g, 1);
            v = gz_take(g, 2);
            if (v == 3) goto bad;
            if (v == 0) {
                gz_take(g, g->nb & 7);
                g->state = GZS_STORED;
            } else if (v == 1) {
                for (int i = 0; i < 288; i++) g->lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                gz_build(&g->lit, g->lens, 288);
                for (int i = 0; i < 30; i++) g->lens[i] = 5;
                gz_build(&g->dst, g->lens, 30);
                g->state = GZS_SYM;
            } else g->state = GZS_DYN;
            break;
        case GZS_STORED:
            if (!gz_bits(g, &in, end, 32)) goto more;
            v = gz_take(g, 32);
            if ((v & 0xFFFF) != (~v >> 16)) goto bad;
            g->left = v & 0xFFFF;
            g->state = GZS_RAW;
            break;
        case GZS_RAW:
            while (g->left) {
                if (in == end) goto more;
                g->left--;
                if (!gz_put(g, *in++, sink, ctx)) goto stop;
            }
            g->state = g->last ? GZS_CRC : GZS_BLOCK;
            break;
        case GZS_DYN:
            if (!gz_bits(g, &in, end, 14)) goto more;
            g->nlen = (int)gz_take(g, 5) + 257;
            g->ndist = (int)gz_take(g, 5) + 1;
            g->ncode = (int)gz_take(g, 4) + 4;
            if (g->nlen > 286 || g->ndist > 30) goto bad;
            for (int i = 0; i < 19; i++) g->lens[i] = 0;
            g->idx = 0;
            g->state = GZS_CLEN;
            break;
        case GZS_CLEN:
            while (g->idx < g->ncode) {
                if (!gz_bits(g, &in, end, 3)) goto more;
                g->lens[order[g->idx++]] = (u8)gz_take(g, 3);
            }
            if (gz_build(&g->lit, g->lens, 19)) goto bad;
            g->idx = 0;
            g->state = GZS_LENS;
            break;
        case GZS_LENS:
            while (g->idx < g->nlen + g->ndist) {
                s = gz_sym(g, &g->lit, &in, end);
                if (s == -1) goto more;
                if (s < 0) goto bad;
                if (s >= 16) { g->sym = s; g->state = GZS_REP; break; }
                g->lens[g->idx++] = (u8)s;
            }
            if (g->state == GZS_REP) break;
            if (!g->lens[256]) goto bad;
            r = gz_build(&g->dst, g->lens + g->nlen, g->ndist);
            if (r < 0 || (r > 0 && g->ndist - g->dst.count[0] != g->dst.count[1])) goto bad;
            r = gz_build(&g->lit, g->lens, g->nlen);
            if (r < 0 || (r > 0 && g->nlen - g->lit.count[0] != g->lit.count[1])) goto bad;
            g->state = GZS_SYM;
            break;
        case GZS_REP:
            s = g->sym == 16 ? 2 : g->sym == 17 ? 3 : 7;
            if (g->sym == 16 && !g->idx) goto bad;
            if (!gz_bits(g, &in, end, s)) goto more;
            r = (int)gz_take(g, s) + (g->sym == 18 ? 11 : 3);
            if (g->idx + r > g->nlen + g->ndist) goto bad;
            v = g->sym == 16 ? g->lens[g->idx - 1] : 0;
            while (r--) g->lens[g->idx++] = (u8)v;
            g->state = GZS_LENS;
            break;
        case GZS_SYM:
            for (;;) {
                s = gz_sym(g, &g->lit, &in, end);
                if (s == -1) goto more;
                if (s < 0) goto bad;
                if (s >= 256) break;
                if (!gz_put(g, (u8)s, sink, ctx)) goto stop;
            }
            if (s == 256) {
                g->state = g->last ? GZS_CRC : GZS_BLOCK;
                break;
            }
            if (s > 285) goto bad;
            g->sym = s - 257;
            g->state = GZS_LEXT;
            break;
        case GZS_LEXT:
            if (!gz_bits(g, &in, end, lext[g->sym])) goto more;
            g->len = lbase[g->sym] + (int)gz_take(g, lext[g->sym]);
            g->state = GZS_DIST;
            break;
        case GZS_DIST:
            s = gz_sym(g, &g->dst, &in, end);
            if (s == -1) goto more;
            if (s < 0 || s > 29) goto bad;
            g->sym = s;
            g->state = GZS_DEXT;
            break;
        case GZS_DEXT:
            if (!gz_bits(g, &in, end, dext[g->sym])) goto more;
            v = dbase[g->sym] + gz_take(g, dext[g->sym]);
            if (v > g->total || v > GZ_WIN) goto bad;
            while (g->len--)
                if (!gz_put(g, g->o[(g->wpos - v) & g->omask], sink, ctx)) goto stop;
            g->state = GZS_SYM;
            break;
        case GZS_CRC:
            gz_take(g, g->nb & 7);
            if (!gz_bits(g, &in, end, 32)) goto more;
            if (!gz_flush(g, sink, ctx)) goto stop;
            if (gz_take(g, 32) != g->crc) goto bad;
            g->state = GZS_SIZE;
            break;
        case GZS_SIZE:
            if (!gz_bits(g, &in, end, 32)) goto more;
            if (gz_take(g, 32) != g->total) goto bad;
            g->state = GZS_END;
            break;
        default:
            if (in == end) return GZ_DONE;
            if (*in != 0x1F) return GZ_DONE;
            g->idx = 0;
            g->state = GZS_HEAD;
            break;
        }
    }
more:
    if (gz_flush(g, sink, ctx)) return GZ_MORE;
stop:
    return g->state = GZ_STOP;
bad:
    return g->state = GZ_ERR;
}
static inline int gz_unpack(Gunzip *g, const u8 *src, u32 n, u8 *dst, u32 cap)
{
    gz_init(g, dst);
    g->omask = 0xFFFFFFFFu;
    g->room = cap;
    if (!cap || gz_feed(g, src, n, 0, 0) != GZ_DONE) return -1;
    return (int)g->wpos;
}
