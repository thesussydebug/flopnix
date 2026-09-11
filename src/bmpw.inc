/* Writes an 8-bit BMP image. */
#pragma once

#define BMPW_HDR (54 + 1024)

static inline int bmpw_rowsz(int w) { return (w + 3) & ~3; }

static inline void bmpw_32(u8 *d, u32 v)
{
    d[0] = (u8)v; d[1] = (u8)(v >> 8); d[2] = (u8)(v >> 16); d[3] = (u8)(v >> 24);
}

static inline u32 bmpw_head(u8 *d, int w, int h, const u8 *pal)
{
    u32 imgsz = (u32)bmpw_rowsz(w) * (u32)h;
    u32 fsz = BMPW_HDR + imgsz;
    for (int i = 0; i < 54; i++) d[i] = 0;
    d[0] = 'B'; d[1] = 'M';
    bmpw_32(d + 2, fsz);
    bmpw_32(d + 10, BMPW_HDR);
    bmpw_32(d + 14, 40);
    bmpw_32(d + 18, (u32)w);
    bmpw_32(d + 22, (u32)h);
    d[26] = 1;
    d[28] = 8;
    bmpw_32(d + 34, imgsz);
    bmpw_32(d + 38, 2835); bmpw_32(d + 42, 2835);
    bmpw_32(d + 46, 256);
    for (int i = 0; i < 256; i++) {
        d[54 + i * 4]     = pal[i * 3 + 2];
        d[54 + i * 4 + 1] = pal[i * 3 + 1];
        d[54 + i * 4 + 2] = pal[i * 3];
        d[54 + i * 4 + 3] = 0;
    }
    return fsz;
}

static inline u8 *bmpw_row(u8 *d, int w, int h, int y)
{
    return d + BMPW_HDR + (u32)(h - 1 - y) * (u32)bmpw_rowsz(w);
}
