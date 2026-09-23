/* Reads BMP headers and scales image pixels. */
#pragma once

typedef struct {
    int w, h;
    int topdown;
    u32 bpp;
    u32 off;
    u32 rowsz;
    u32 paloff;
} BmpHead;

static inline u32 bmp_u16(const u8 *d) { return (u32)d[0] | ((u32)d[1] << 8); }
static inline u32 bmp_u32(const u8 *d)
{
    return (u32)d[0] | ((u32)d[1] << 8) | ((u32)d[2] << 16) | ((u32)d[3] << 24);
}

#define BMP_MAXDIM 0x10000

static inline int bmp_head(const u8 *bm, u32 n, BmpHead *o)
{
    if (!bm || !o || n < 54) return -1;
    if (bm[0] != 'B' || bm[1] != 'M') return -1;
    if (bmp_u32(bm + 30) != 0) return -1;
    u32 dib = bmp_u32(bm + 14);
    if (dib < 40 || dib > n - 14 || bmp_u16(bm + 26) != 1) return -1;

    int w = (int)bmp_u32(bm + 18);
    int h = (int)bmp_u32(bm + 22);
    u32 bpp = bmp_u16(bm + 28);
    if (bpp != 8 && bpp != 24) return -1;

    if (w <= 0 || w > BMP_MAXDIM || !h || h < -BMP_MAXDIM || h > BMP_MAXDIM) return -1;
    int topdown = h < 0;
    if (topdown) h = -h;

    o->w = w;
    o->h = h;
    o->topdown = topdown;
    o->bpp = bpp;
    o->off = bmp_u32(bm + 10);
    o->rowsz = bpp == 8 ? (((u32)w + 3) & ~3u) : ((((u32)w * 3) + 3) & ~3u);
    o->paloff = 14 + dib;
    return 0;
}

static inline int bmp_row_ok(const BmpHead *o, int row, u32 n)
{
    if (!o || row < 0 || row >= o->h || !o->rowsz || o->off > n) return 0;
    return (u32)row < (n - o->off) / o->rowsz;
}

static inline int bmp_src_row(const BmpHead *o, int y)
{
    return o->topdown ? y : (o->h - 1 - y);
}

static inline int bmp_scale(int i, int dstn, int srcn)
{
    if (i < 0 || dstn <= 0 || srcn <= 0) return 0;
    int s = (int)(((u32)i * (u32)srcn) / (u32)dstn);
    return s >= srcn ? srcn - 1 : s;
}
