#pragma once
typedef struct { const u8 *name; u32 nlen, method, crc, packed, raw, at; u16 flags; } ZipEnt;
typedef struct { char name[260]; u32 size, at; int file; } TarEnt;

static int ari_fit(const char *path, int len, char out[24])
{
    int from = 0, n, dot = -1, o = 0;
    for (int i = 0; i < len; i++)
        if (path[i] == '/' || path[i] == '\\' || path[i] == ':') from = i + 1;
    path += from;
    n = len - from;
    while (n && (path[n - 1] == ' ' || path[n - 1] == '.')) n--;
    if (n <= 0) return 0;
    for (int i = 1; i < n; i++)
        if (path[i] == '.') dot = i;
    int ext = dot > 0 && n - dot <= 9 ? n - dot : 0;
    int stem = n - ext;
    if (stem > 23 - ext) stem = 23 - ext;
    for (int i = 0; i < stem + ext; i++) {
        u8 c = (u8)path[i < stem ? i : n - ext + i - stem];
        if (c < 32 || c > 126 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        out[o++] = (char)c;
    }
    while (o && (out[o - 1] == ' ' || out[o - 1] == '.')) o--;
    out[o] = 0;
    if (out[0] == '.') out[0] = '_';
    return o > 0;
}

static int ari_unique(char name[24], const ArEntry *e, int count)
{
    char base[24];
    int n = 0, dot = -1;
    for (; name[n]; n++) base[n] = name[n];
    base[n] = 0;
    for (int i = 1; i < n; i++)
        if (base[i] == '.') dot = i;
    int stem = dot > 0 ? dot : n;
    for (int k = 1; k <= 9; k++) {
        int clash = 0;
        for (int i = 0; i < count && !clash; i++) clash = ar_same(e[i].name, name);
        if (!clash) return 1;
        if (k == 9) break;
        int keep = stem, o = 0;
        if (keep + 2 + (n - stem) > 23) keep = 23 - 2 - (n - stem);
        if (keep < 1) return 0;
        for (int i = 0; i < keep; i++) name[o++] = base[i];
        name[o++] = '~';
        name[o++] = (char)('1' + k);
        for (int i = stem; i < n; i++) name[o++] = base[i];
        name[o] = 0;
    }
    return 0;
}

static u32 ari_u16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 ari_u32(const u8 *p) { return ari_u16(p) | ari_u16(p + 2) << 16; }

static int zip_is(const u8 *d, u32 n)
{
    return n >= 22 && d[0] == 'P' && d[1] == 'K' && ((d[2] == 3 && d[3] == 4) || (d[2] == 5 && d[3] == 6));
}

static int zip_open(const u8 *d, u32 n, u32 *at, u32 *count)
{
    if (n < 22) return 0;
    u32 low = n > 22 + 65535 ? n - 22 - 65535 : 0;
    for (u32 p = n - 22; ; p--) {
        if (d[p] == 'P' && d[p + 1] == 'K' && d[p + 2] == 5 && d[p + 3] == 6) {
            u32 total = ari_u16(d + p + 10), size = ari_u32(d + p + 12), start = ari_u32(d + p + 16);
            if (ari_u16(d + p + 4) || ari_u16(d + p + 6) || ari_u16(d + p + 8) != total) return 0;
            if (total == 0xFFFF || start > p || size > p - start) return 0;
            *at = start;
            *count = total;
            return 1;
        }
        if (p == low) break;
    }
    return 0;
}

static int zip_next(const u8 *d, u32 n, u32 *at, ZipEnt *e)
{
    u32 p = *at;
    if (p > n || n - p < 46 || d[p] != 'P' || d[p + 1] != 'K' || d[p + 2] != 1 || d[p + 3] != 2) return 0;
    u32 nlen = ari_u16(d + p + 28), extra = ari_u16(d + p + 30) + ari_u16(d + p + 32), local = ari_u32(d + p + 42);
    if (nlen + extra > n - p - 46) return 0;
    e->flags = (u16)ari_u16(d + p + 8);
    e->method = ari_u16(d + p + 10);
    e->crc = ari_u32(d + p + 16);
    e->packed = ari_u32(d + p + 20);
    e->raw = ari_u32(d + p + 24);
    e->name = d + p + 46;
    e->nlen = nlen;
    if (local > n || n - local < 30 || d[local] != 'P' || d[local + 1] != 'K' || d[local + 2] != 3 || d[local + 3] != 4) return 0;
    u32 skip = 30 + ari_u16(d + local + 26) + ari_u16(d + local + 28);
    if (skip > n - local || e->packed > n - local - skip) return 0;
    e->at = local + skip;
    *at = p + 46 + nlen + extra;
    return 1;
}

static u32 zip_gzip(const ZipEnt *e, const u8 *d, u8 *out)
{
    static const u8 head[10] = { 0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 255 };
    u32 o = 0;
    for (; o < 10; o++) out[o] = head[o];
    for (u32 i = 0; i < e->packed; i++) out[o++] = d[e->at + i];
    for (int i = 0; i < 4; i++) out[o++] = (u8)(e->crc >> (i * 8));
    for (int i = 0; i < 4; i++) out[o++] = (u8)(e->raw >> (i * 8));
    return o;
}

static int tar_sum(const u8 *h)
{
    u32 sum = 0, want = 0;
    int i = 148;
    for (int k = 0; k < 512; k++) sum += k >= 148 && k < 156 ? ' ' : h[k];
    while (i < 156 && h[i] == ' ') i++;
    for (; i < 156 && h[i] >= '0' && h[i] <= '7'; i++) want = want * 8 + (u32)(h[i] - '0');
    return sum == want;
}

static int tar_is(const u8 *d, u32 n)
{
    return n >= 512 && d[0] && tar_sum(d);
}

static int tar_next(const u8 *d, u32 n, u32 *at, TarEnt *e)
{
    int named = 0;
    for (;;) {
        u32 p = *at, size = 0;
        if (p > n || n - p < 512) return 0;
        const u8 *h = d + p;
        int zero = 1;
        for (int i = 0; i < 512 && zero; i++) zero = !h[i];
        if (zero) return 0;
        if (!tar_sum(h) || (h[124] & 0x80)) return -1;
        for (int i = 124; i < 136; i++) {
            if (h[i] == ' ' || !h[i]) { if (size) break; continue; }
            if (h[i] < '0' || h[i] > '7' || size > 0x0FFFFFFFu) return -1;
            size = size * 8 + (u32)(h[i] - '0');
        }
        u32 span = (size + 511) & ~511u;
        if (span < size || span > n - p - 512) return -1;
        int type = h[156];
        *at = p + 512 + span;
        if (type == 'L') {
            u32 k = 0;
            for (; k < size && k < sizeof e->name - 1 && d[p + 512 + k]; k++) e->name[k] = (char)d[p + 512 + k];
            e->name[k] = 0;
            named = 1;
            continue;
        }
        if (type == 'x' || type == 'g' || type == 'K') continue;
        if (!named) {
            u32 k = 0;
            if (h[257] == 'u' && h[258] == 's' && h[259] == 't' && h[260] == 'a' && h[261] == 'r' && h[262] != ' ' && h[345]) {
                for (u32 i = 0; i < 155 && h[345 + i]; i++) e->name[k++] = (char)h[345 + i];
                e->name[k++] = '/';
            }
            for (u32 i = 0; i < 100 && h[i]; i++) e->name[k++] = (char)h[i];
            e->name[k] = 0;
        }
        e->file = type == '0' || !type || type == '7';
        e->size = size;
        e->at = p + 512;
        return 1;
    }
}
