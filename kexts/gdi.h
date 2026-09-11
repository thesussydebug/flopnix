/* Types and calls for the 2D drawing service. */
#pragma once
#include "kapi.h"

typedef u32 GRGB;
#define GRGB(r,g,b) (((u32)(u8)(r)<<16)|((u32)(u8)(g)<<8)|(u32)(u8)(b))
#define GIDX(i)     (0xFF000000u | (u32)(u8)(i))

typedef i32 fx;
#define FX_ONE    65536
#define FX_FRAC   0xFFFF
#define FX(n)     ((fx)((n) * 65536))
#define FX_INT(v) ((v) >> 16)

static inline fx fx_mul(fx a, fx b)
{
    fx r;
    __asm__ ("imull %2\n\tshrdl $16, %%edx, %%eax"
             : "=a"(r) : "a"(a), "rm"(b) : "edx", "cc");
    return r;
}

static inline fx fx_div(fx a, fx b)
{
    fx q;
    if (b == 0) return a < 0 ? (fx)0x80000000 : (fx)0x7FFFFFFF;
    { i32 aa = a < 0 ? -a : a, ab = b < 0 ? -b : b;
      if (((u32)aa >> 15) >= (u32)ab) return (a ^ b) < 0 ? (fx)0x80000000
                                                         : (fx)0x7FFFFFFF; }
    __asm__ ("idivl %3" : "=a"(q) : "a"(a << 16), "d"(a >> 16), "rm"(b) : "cc");
    return q;
}

static inline u32 isqrt(u32 v)
{
    u32 root = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= root + bit) { v -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}

static inline fx fx_sqrt(fx x)
{
    return x <= 0 ? 0 : (fx)(isqrt((u32)x) << 8);
}

static inline fx fx_lerp(fx a, fx b, fx t) { return a + fx_mul(b - a, t); }

static inline fx fx_clamp(fx v, fx lo, fx hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int fx_ceil(fx v)
{
    return -((-v) >> 16);
}

static inline u8 gdi_nearest(int r, int g, int b, const u32 *rgb, int n)
{
    int best = 0;
    u32 bestd = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) {
        int dr = r - (int)((rgb[i] >> 16) & 0xFF);
        int dg = g - (int)((rgb[i] >> 8) & 0xFF);
        int db = b - (int)(rgb[i] & 0xFF);
        u32 d = (u32)(dr * dr + dg * dg + db * db);
        if (d < bestd) { bestd = d; best = i; }
    }
    return (u8)best;
}

#define GDI_RAMPS   8
#define GDI_RAMP_N  24
#define GDI_RAMP_BASE 48
static inline u32 gdi_ramp_entry(int ramp, int shade)
{
    static const u32 hue[GDI_RAMPS] = {
        0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF,
        0xFF00FF, 0xFFFF00, 0xFFFFFF, 0xFF8000,
    };
    u32 h = hue[ramp & 7], m = (u32)(shade + 1);
    u32 r = (((h >> 16) & 0xFF) * m) / GDI_RAMP_N;
    u32 g = (((h >> 8) & 0xFF) * m) / GDI_RAMP_N;
    u32 b = ((h & 0xFF) * m) / GDI_RAMP_N;
    return (r << 16) | (g << 8) | b;
}

static inline int gdi_bayer(int x, int y)
{
    static const u8 m[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 },
    };
    return m[y & 3][x & 3];
}

#define GDI_ABI 11
enum { GCAP_BPP, GCAP_WIDTH, GCAP_HEIGHT, GCAP_PRESENT_MODE,
       GCAP_FPU, GCAP_HEAP_FREE, GCAP_PALETTE_FREE,
       GCAP_AA,
       GCAP_LUT_US,
       GCAP_DITHER,
       GCAP_OBJECTS };

typedef struct { i32 x, y; } GPt;
enum { GPF_EVENODD, GPF_WINDING };

typedef struct { i32 l, t, r, b; } GRect;
enum { RGN_AND, RGN_OR, RGN_XOR, RGN_DIFF, RGN_COPY };
enum { GRGN_NULL, GRGN_SIMPLE, GRGN_COMPLEX };
typedef struct GRgn GRgn;
enum { GP_SPAN, GP_SURFACE, GP_DIRECT };
enum { GR2_COPY, GR2_XOR, GR2_AND, GR2_OR, GR2_NOT };

typedef struct {
    u32 abi;

    int (*fill)(int x, int y, int w, int h, u8 idx);
    int (*fill_rgb)(int x, int y, int w, int h, GRGB c);
    int (*blit)(int x, int y, int w, int h, const u8 *src, int spitch);
    int (*rop2)(int x, int y, int w, int h, u8 idx, int op);
    int (*caps)(int what);

    int  (*pal_build)(void);
    u8   (*pal_index)(GRGB c);
    u32  (*pal_unpack)(u8 idx);
    int  (*fill_gradient)(int x, int y, int w, int h,
                          GRGB a, GRGB b, int vertical);

    void (*set_dither)(int on);

    GRgn *(*rgn_rect)(int l, int t, int r, int b);
    void  (*rgn_free)(GRgn *r);
    int   (*rgn_combine)(GRgn *dst, const GRgn *a, const GRgn *b, int mode);
    int   (*rgn_offset)(GRgn *r, int dx, int dy);
    int   (*rgn_pt_in)(const GRgn *r, int x, int y);
    int   (*rgn_box)(const GRgn *r, GRect *bbox);
    int   (*rgn_bands)(const GRgn *r, GRect *out, int max);
    int   (*rgn_equal)(const GRgn *a, const GRgn *b);
    int   (*fill_rgn)(const GRgn *r, GRGB c);

    int   (*fill_poly)(const GPt *pts, int n, GRGB c);

    GRgn *(*rgn_polygon)(const GPt *pts, int n, int mode);

    int   (*fill_beziers)(const GPt *ctrl, int nseg, GRGB c);

    int   (*stroke_poly)(const GPt *pts, int n, int closed, int width, GRGB c);

    int   (*fill_poly_aa)(const GPt *pts, int n, GRGB c);

    int   (*draw_line)(int x0, int y0, int x1, int y1, GRGB c);

    int   (*pal_ramps)(void);
} GdiOps;

static inline const GdiOps *gdi_bind(const Kapi *k, u32 min_abi)
{
    const GdiOps *g = (const GdiOps *)k->service_get("gdi");
    return (g && g->abi >= min_abi) ? g : 0;
}
