/* Decodes PNG, JPEG, GIF and BMP into 8-bit palette pixels for other extensions. */
#include "kapi.h"
#include "image.h"
#include "imgcore.inc"

static const Kapi *api;

/* Every decoder allocation is listed so a failed, cancelled or faulted decode frees it all. */
typedef struct Blk { struct Blk *next, *prev; u32 size, pad; } Blk;
static Blk *blocks;
static u32 used, limit;
static u8 over, oom, cancel;
static ImgReq *req;
static u32 step_tick;
static int step_frac;

static void blk_link(Blk *b)
{
    b->prev = 0; b->next = blocks;
    if (blocks) blocks->prev = b;
    blocks = b;
}
static void blk_unlink(Blk *b)
{
    if (b->prev) b->prev->next = b->next; else blocks = b->next;
    if (b->next) b->next->prev = b->prev;
}
static int room(u32 n)
{
    if (n <= 0x7fff0000u && n <= limit - used) return 1;
    over = 1;
    return 0;
}
static void *ia_alloc(u32 n)
{
    if (!room(n)) return 0;
    Blk *b = api->kmalloc(n + sizeof *b);
    while (!b && req && req->reclaim && req->reclaim(req->ctx)) b = api->kmalloc(n + sizeof *b);
    if (!b) { oom = 1; return 0; }
    b->size = n;
    blk_link(b);
    used += n;
    return b + 1;
}
static void ia_free(void *p)
{
    if (!p) return;
    Blk *b = (Blk *)p - 1;
    blk_unlink(b);
    used -= b->size;
    api->kfree(b);
}
static void *ia_realloc(void *p, u32 n)
{
    if (!p) return ia_alloc(n);
    Blk *b = (Blk *)p - 1;
    u32 old = b->size;
    if (n > old && !room(n - old)) return 0;
    blk_unlink(b);
    Blk *nb = api->krealloc(b, n + sizeof *b);
    while (!nb && req && req->reclaim && req->reclaim(req->ctx)) nb = api->krealloc(b, n + sizeof *b);
    if (!nb) { blk_link(b); oom = 1; return 0; }
    nb->size = n;
    blk_link(nb);
    used = used - old + n;
    return nb + 1;
}
static void ia_free_all(void)
{
    while (blocks) ia_free(blocks + 1);
    used = 0;
}

#define STBI_FLOPNIX
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS IMG_BAND_DIM
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(n) ia_alloc((u32)(n))
#define STBI_REALLOC(p,n) ia_realloc(p,(u32)(n))
#define STBI_FREE(p) ia_free(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define memcpy(d,s,n) api->memcpy(d,s,n)
#define memset(d,c,n) api->memset(d,c,n)
#define memmove(d,s,n) api->memmove(d,s,n)
#define abs(n) ((n)<0?-(n):(n))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "vendor/stb_image.h"
#pragma clang diagnostic pop
#undef memcpy
#undef memset
#undef memmove
#undef abs

static int img_step(int done, int total)
{
    if (cancel) return 1;
    if (!req || !req->progress) return 0;
    u32 t = *api->ticks;
    if (t == step_tick) return 0;
    step_tick = t;
    if (total > 0) {
        int f = (int)((u32)done * 256u / (u32)total);
        if (f > step_frac) step_frac = f;
    }
    if (req->progress(req->ctx, step_frac)) cancel = 1;
    return cancel;
}

static void start(u32 budget, ImgReq *q)
{
    ia_free_all();
    limit = budget; over = oom = cancel = 0; req = q;
    step_tick = *api->ticks - 1; step_frac = 0;
}

static int failed(void)
{
    if (cancel) return IMG_ECANCEL;
    if (oom) return IMG_ENOMEM;
    if (over) return IMG_ETOOBIG;
    const char *r = stbi__g_failure_reason;
    return r && !api->strcmp(r, "too large") ? IMG_ETOOBIG : IMG_ECORRUPT;
}

static int img_format(const u8 *d, u32 n)
{
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return IMG_PNG;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return IMG_JPEG;
    if (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return IMG_GIF;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return IMG_BMP;
    return 0;
}

static int probe_locked(const u8 *data, u32 len, ImgInfo *out, u32 budget, ImgReq *q)
{
    int f = img_format(data, len);
    if (!f || len > 0x7fffffffu) return IMG_EFORMAT;
    start(budget, q);
    stbi__g_failure_reason = 0;
    stbi__context s;
    stbi__start_mem(&s, data, (int)len);
    int w, h, c, ok;
    if (f == IMG_PNG) ok = stbi__png_info(&s, &w, &h, &c);
    else if (f == IMG_JPEG) ok = stbi__jpeg_info(&s, &w, &h, &c);
    else if (f == IMG_GIF) ok = stbi__gif_info(&s, &w, &h, &c);
    else ok = stbi__bmp_info(&s, &w, &h, &c);
    if (!ok) return failed();
    if (f == IMG_BMP && h < 0 && h > -IMG_MAX_DIM - 1) h = -h;
    if (w < 1 || h < 1) return IMG_ECORRUPT;
    if (f != IMG_JPEG && (w > IMG_MAX_DIM || h > IMG_MAX_DIM)) return IMG_ETOOBIG;
    out->w = w; out->h = h; out->format = f; out->has_alpha = c == 2 || c == 4;
    return IMG_OK;
}

static void band(ImgReq *q, int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++) api->memset(q->dst + (u32)(y + j) * (u32)q->pitch + (u32)x, q->bg, (u32)w);
}

typedef struct { const ImgMap *m; int next; } RowSink;

static void row_sink(void *ctx, int y, const u8 *row)
{
    RowSink *s = ctx;
    const ImgMap *m = s->m;
    while (s->next < m->fh && s->next * m->sh / m->fh == y) img_map_row(m, s->next++, row);
}

/* 1 when the file needs the whole picture in memory instead. */
static int jpeg_rows(const u8 *data, u32 len, const ImgMap *m, u32 budget, ImgReq *q, int *r)
{
    RowSink sink = { m, 0 };
    stbi__context s;
    start(budget, q);
    stbi__g_failure_reason = 0;
    stbi__start_mem(&s, data, (int)len);
    stbi__flopnix_step = img_step;
    int ok = stbi__flopnix_jpeg_band(&s, 3, row_sink, &sink);
    stbi__flopnix_step = 0;
    const char *why = stbi__g_failure_reason;
    if (!ok && !cancel && !oom && !over && why && !api->strcmp(why, "band")) { ia_free_all(); return 1; }
    *r = ok ? IMG_OK : failed();
    return 0;
}

static int decode_locked(const u8 *data, u32 len, ImgReq *q)
{
    ImgInfo in;
    u32 budget = q->budget ? q->budget : img_tier(api->mem_total_kb());
    int r = probe_locked(data, len, &in, budget, q);
    if (r) return r;
    int ch = in.has_alpha ? 4 : 3, w = in.w, h = in.h;
    if (in.format != IMG_JPEG && !img_fits(w, h, ch, budget)) return IMG_ETOOBIG;
    u16 *cache = api->kmalloc(4096 * sizeof *cache);
    if (!cache) return IMG_ENOMEM;
    int fw = q->dw, fh = q->dh, ox = 0, oy = 0;
    if (q->flags & IMG_FIT) {
        img_fit(w, h, q->dw, q->dh, &fw, &fh);
        ox = (q->dw - fw) / 2; oy = (q->dh - fh) / 2;
    }
    u8 br, bgc, bb;
    api->palette_rgb(q->bg, &br, &bgc, &bb);
    ImgMap m = { 0, w, h, ch, q->dst, q->pitch, ox, oy, fw, fh, q->flags, q->key, br, bgc, bb,
                 cache, api->palette_nearest };
    img_map_begin(&m);
    if (in.format != IMG_JPEG || jpeg_rows(data, len, &m, budget, q, &r)) {
        r = IMG_ETOOBIG;
        if (img_fits(w, h, ch, budget)) {
            start(budget, q);
            int c;
            stbi__flopnix_step = img_step;
            u8 *px = stbi_load_from_memory(data, (int)len, &w, &h, &c, ch);
            stbi__flopnix_step = 0;
            r = !px ? failed() : w != in.w || h != in.h ? IMG_ECORRUPT : IMG_OK;
            m.src = px;
            if (!r) img_map(&m);
        }
    }
    api->kfree(cache);
    if (r) return r;
    if (q->flags & IMG_FIT) {
        band(q, 0, 0, q->dw, oy);
        band(q, 0, oy + fh, q->dw, q->dh - oy - fh);
        band(q, 0, oy, ox, fh);
        band(q, ox + fw, oy, q->dw - ox - fw, fh);
    }
    q->out_w = fw; q->out_h = fh;
    return IMG_OK;
}

static int img_probe(const u8 *data, u32 len, ImgInfo *out)
{
    if (!data || !out) return IMG_EFORMAT;
    api->buffer_lock();
    int r = probe_locked(data, len, out, img_tier(api->mem_total_kb()), 0);
    ia_free_all();
    api->buffer_unlock();
    return r;
}

static int img_decode(const u8 *data, u32 len, ImgReq *q)
{
    if (!data || !q || !q->dst || q->dw < 1 || q->dh < 1 || q->pitch < q->dw) return IMG_EFORMAT;
    q->out_w = q->out_h = 0;
    api->buffer_lock();
    int r = decode_locked(data, len, q);
    ia_free_all();
    req = 0;
    api->buffer_unlock();
    return r;
}

static const char *img_error(int code)
{
    switch (code) {
    case IMG_OK:       return "OK";
    case IMG_EFORMAT:  return "Not a PNG, JPEG, GIF or BMP image";
    case IMG_ETOOBIG:  return "The image is too large for this computer";
    case IMG_ENOMEM:   return "Not enough memory to open the image";
    case IMG_ECORRUPT: return "The image file is damaged";
    case IMG_ECANCEL:  return "Cancelled";
    }
    return "Unknown image error";
}

static const ImageOps ops = { IMAGE_ABI, img_probe, img_decode, img_error };

const KextHeader kext_header = { KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, KEXT_ON_DEMAND, "Image" };

int kext_entry(const Kapi *k)
{
    api = k;
    return k->register_service("image", &ops);
}
