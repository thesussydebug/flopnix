/* Shared image decoder library (image.kx): PNG, JPEG, GIF and BMP to 8-bit palette pixels. */
#pragma once
#include "kapi.h"

#define IMAGE_ABI 1
enum { IMG_BMP = 1, IMG_PNG, IMG_JPEG, IMG_GIF };
enum { IMG_OK = 0, IMG_EFORMAT = -1, IMG_ETOOBIG = -2, IMG_ENOMEM = -3,
       IMG_ECORRUPT = -4, IMG_ECANCEL = -5 };
#define IMG_DITHER   1
#define IMG_FIT      2
#define IMG_ALPHAKEY 4

typedef struct { int w, h, format, has_alpha; } ImgInfo;

typedef struct {
    u8  *dst; int dw, dh, pitch;
    u32  flags; u8 key, bg;
    u32  budget;
    int (*reclaim)(void *ctx);
    int (*progress)(void *ctx, int frac256);
    void *ctx;
    int  out_w, out_h;
} ImgReq;

typedef struct {
    u32 abi;
    int (*probe)(const u8 *data, u32 len, ImgInfo *out);
    int (*decode)(const u8 *data, u32 len, ImgReq *req);
    const char *(*error)(int code);
} ImageOps;

/* attempt to load image.kx, retry after 10s*/
static inline const ImageOps *img_bind(const Kapi *k)
{
    static u32 tried_at;
    static u8 tried;
    const ImageOps *o = (const ImageOps *)k->service_get("image");
    if (!o && (!tried || *k->ticks - tried_at >= 1000)) {
        tried = 1;
        tried_at = *k->ticks;
        if (k->fs_exists("sys/image.kx")) k->kext_load("sys/image.kx");
        o = (const ImageOps *)k->service_get("image");
    }
    return o && o->abi >= IMAGE_ABI ? o : 0;
}

static inline void img_fit(int sw, int sh, int maxw, int maxh, int *dw, int *dh)
{
    if (sw < 1 || sh < 1 || maxw < 1 || maxh < 1) { *dw = *dh = 0; return; }
    if ((u32)maxw * (u32)sh <= (u32)maxh * (u32)sw) {
        *dw = maxw; *dh = (int)((u32)maxw * (u32)sh / (u32)sw);
    } else {
        *dh = maxh; *dw = (int)((u32)maxh * (u32)sw / (u32)sh);
    }
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
}
