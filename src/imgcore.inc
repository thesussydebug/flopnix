/* memory handling and mapping RGB*/
#pragma once
#define IMG_MAX_DIM 2048
#define IMG_BAND_DIM 8192

static inline u32 img_tier(u32 total_kb)
{
    return total_kb >= 16256 ? 4194304u : total_kb >= 7936 ? 1048576u : 262144u;
}

static inline int img_fits(int w, int h, int ch, u32 budget)
{
    return w >= 1 && h >= 1 && w <= IMG_MAX_DIM && h <= IMG_MAX_DIM &&
           (u32)w * (u32)h * (u32)ch <= budget;
}

static inline int img_bayer(int x, int y)
{
    static const u8 m[4][4] = {
        {  0,  8,  2, 10 }, { 12,  4, 14,  6 }, {  3, 11,  1,  9 }, { 15,  7, 13,  5 },
    };
    return m[y & 3][x & 3];
}

static inline int img_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

typedef u8 (*ImgNearest)(u8 r, u8 g, u8 b);

/* Scales src (sw x sh, ch bytes per pixel) into the fw x fh area at ox,oy of dst. */
typedef struct {
    const u8 *src; int sw, sh, ch;
    u8 *dst; int pitch, ox, oy, fw, fh;
    u32 flags; u8 key, bg_r, bg_g, bg_b;
    u16 *cache;
    ImgNearest nearest;
} ImgMap;

static inline void img_map_begin(const ImgMap *m)
{
    for (int i = 0; i < 4096; i++) m->cache[i] = 256;
}

/* Fills output row y from one source row of sw pixels. */
static void img_map_row(const ImgMap *m, int y, const u8 *row)
{
    int step = m->sw / m->fw, rem = m->sw % m->fw;
    {
        u8 *out = m->dst + (u32)(m->oy + y) * (u32)m->pitch + (u32)m->ox;
        int sx = 0, acc = 0;
        for (int x = 0; x < m->fw; x++) {
            const u8 *p = row + (u32)sx * (u32)m->ch;
            sx += step; acc += rem;
            if (acc >= m->fw) { acc -= m->fw; sx++; }
            int r = p[0], g = p[1], b = p[2];
            if (m->ch == 4) {
                int a = p[3];
                if (m->flags & IMG_ALPHAKEY) {
                    if (a < 128) { out[x] = m->key; continue; }
                } else if (a < 255) {
                    r = (r * a + m->bg_r * (255 - a)) / 255;
                    g = (g * a + m->bg_g * (255 - a)) / 255;
                    b = (b * a + m->bg_b * (255 - a)) / 255;
                }
            }
            if (m->flags & IMG_DITHER) {
                int d = (img_bayer(m->ox + x, m->oy + y) - 8) * 5;
                r = img_clamp(r + d); g = img_clamp(g + d); b = img_clamp(b + d);
            }
            int k = (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4);
            if (m->cache[k] > 255) {
                u8 i = m->nearest((u8)r, (u8)g, (u8)b);
                m->cache[k] = (m->flags & IMG_ALPHAKEY) && i == m->key ? C_WHITE : i;
            }
            out[x] = (u8)m->cache[k];
        }
    }
}

static void img_map(const ImgMap *m)
{
    img_map_begin(m);
    for (int y = 0; y < m->fh; y++)
        img_map_row(m, y, m->src + (u32)(y * m->sh / m->fh) * (u32)m->sw * (u32)m->ch);
}
