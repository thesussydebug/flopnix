/* Encodes screen tiles and frame records for the remote viewer. */
enum { ST_SOLID, ST_RAW, ST_BITS1, ST_BITS2, ST_BITS4 };
enum { ST_PALETTE = 1, ST_KEYFRAME = 2 };
#define ST_TILE 16
#define ST_MAX (1 + ST_TILE * ST_TILE)
#define ST_RECORD (2 + ST_MAX)

static inline u32 st_hash(const u8 *src, int pitch, int w, int h)
{
    u32 hash = 2166136261u ^ ((u32)w << 8) ^ (u32)h;
    for (int y = 0; y < h; y++, src += pitch) {
        int x = 0;
        for (; x + 4 <= w; x += 4) {
            u32 v = src[x] | (u32)src[x + 1] << 8 | (u32)src[x + 2] << 16 | (u32)src[x + 3] << 24;
            hash = (hash ^ v) * 16777619u;
            hash ^= hash >> 15;
        }
        for (; x < w; x++) {
            hash = (hash ^ src[x]) * 16777619u;
            hash ^= hash >> 15;
        }
    }
    return hash;
}

/* map is 256 bytes of zeroes on entry and is left zeroed. */
static inline int st_encode(const u8 *src, int pitch, int w, int h, u8 *out, u8 *map)
{
    u8 cols[16];
    int n = 0, len;
    for (int y = 0; y < h && n <= 16; y++)
        for (int x = 0; x < w; x++) {
            u8 c = src[y * pitch + x];
            if (map[c]) continue;
            if (n == 16) { n = 17; break; }
            cols[n] = c;
            map[c] = (u8)++n;
        }
    if (n == 1) {
        out[0] = ST_SOLID; out[1] = cols[0]; len = 2;
    } else if (n > 16) {
        u8 *p = out + 1;
        out[0] = ST_RAW;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) *p++ = src[y * pitch + x];
        len = (int)(p - out);
    } else {
        int bits = n == 2 ? 1 : n <= 4 ? 2 : 4, colors = 1 << bits, have = 0;
        u32 acc = 0;
        out[0] = bits == 1 ? ST_BITS1 : bits == 2 ? ST_BITS2 : ST_BITS4;
        for (int i = 0; i < colors; i++) out[1 + i] = i < n ? cols[i] : 0;
        u8 *p = out + 1 + colors;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                acc = acc << bits | (u32)(map[src[y * pitch + x]] - 1);
                have += bits;
                if (have == 8) { *p++ = (u8)acc; acc = 0; have = 0; }
            }
        if (have) *p++ = (u8)(acc << (8 - have));
        len = (int)(p - out);
    }
    for (int i = 0; i < n && i < 16; i++) map[cols[i]] = 0;
    return len;
}

static inline void st_u16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

static inline int st_header(u8 *out, int w, int h, int mx, int my, int flags)
{
    out[0] = 'F'; out[1] = 'X'; out[2] = 'S'; out[3] = '1';
    st_u16(out + 4, (u32)w); st_u16(out + 6, (u32)h);
    st_u16(out + 8, (u32)mx); st_u16(out + 10, (u32)my);
    out[12] = (u8)flags; out[13] = ST_TILE;
    return 14;
}

static inline int st_record(u8 *out, int index, const u8 *src, int pitch, int w, int h, u8 *map)
{
    st_u16(out, (u32)index);
    return 2 + st_encode(src, pitch, w, h, out + 2, map);
}

static inline int st_end(u8 *out) { out[0] = out[1] = 0xFF; return 2; }
