#include "kapi.h"

#pragma clang attribute push(__attribute__((minsize)), apply_to = function)
#include "bmpw.inc"
#include "shotname.inc"
#include "shotrect.inc"
#include "pngw.inc"

static const Kapi *api;
static u8 *pal;
static int on_usb, sx, sy;

static int shot_exists(const char *name, void *ctx)
{
    (void)ctx;
    if (!on_usb) return api->fs_exists(name);
    char p[16];
    p[0] = '/';
    api->strlcpy(p + 1, name, 15);
    return api->fat_exists(p) != 0;
}

static u32 shot_png(int w, int h)
{
    u8 *io = api->iobuf;
    u32 raw = (u32)h * (u32)(w + 1);
    if (raw + PW_WORK + 65536 > api->iobuf_size) return 0;
    u8 *work = pal - PW_WORK, *rows = work - raw;
    for (int y = 0; y < h; y++) {
        u8 *r = rows + (u32)y * (u32)(w + 1);
        r[0] = 0;
        api->read_rect(sx, sy + y, w, 1, r + 1, w);
    }
    return pngw_encode(io, (u32)(rows - io), rows, w, h, pal, api->crc32, work);
}

static u32 shot_bmp(int w, int h)
{
    u8 *io = api->iobuf;
    int rowsz = bmpw_rowsz(w);
    if (BMPW_HDR + (u32)rowsz * (u32)h > api->iobuf_size - 768) return 0;
    for (int y = 0; y < h; y++) {
        u8 *r = bmpw_row(io, w, h, y);
        api->read_rect(sx, sy + y, w, 1, r, rowsz);
        for (int x = w; x < rowsz; x++) r[x] = 0;
    }
    return bmpw_head(io, w, h, pal);
}

static void shot_take(int window)
{
    int w = *api->screen_w, h = *api->screen_h, rc;
    u32 keep_bmp = 0, size = 0;
    const char *ext = "PNG";
    char name[16], path[16], msg[48];

    sx = sy = 0;
    for (int i = 0; window && i < api->win_max(); i++) {
        const Win *v = api->win_slot(i);
        if (!v || !api->win_is_focused((Win *)v)) continue;
        int x = v->x, y = v->y, vw = v->w, vh = v->h;
        if (shot_clip(w, h, &x, &y, &vw, &vh)) { sx = x; sy = y; w = vw; h = vh; }
        break;
    }
    api->config_get("shot.bmp", &keep_bmp);
    api->buffer_lock();
    pal = api->iobuf + api->iobuf_size - 768;
    for (int i = 0; i < 256; i++)
        api->palette_rgb(i, &pal[i * 3], &pal[i * 3 + 1], &pal[i * 3 + 2]);
    if (keep_bmp != 1) size = shot_png(w, h);
    if (!size || size >= BMPW_HDR + (u32)bmpw_rowsz(w) * (u32)h) {
        size = shot_bmp(w, h);
        ext = "BMP";
    }
    on_usb = api->fat_mount();
    if (!size) {
        api->buffer_unlock();
        api->notify("screenshot failed (screen too large)");
        return;
    }
    if (!shot_next(shot_exists, 0, ext, name, sizeof name)) {
        api->buffer_unlock();
        api->notify("no free SHOTnn name");
        return;
    }
    api->busy_set("Screenshot", name, -1);
    if (on_usb) {
        path[0] = '/';
        api->strlcpy(path + 1, name, 15);
        rc = api->fat_write(path, api->iobuf, size);
    } else rc = api->fs_write(name, api->iobuf, size);
    api->busy_end();
    api->buffer_unlock();
    if (rc == 0) {
        api->kfmt(msg, sizeof msg, "saved %s to %s", name, on_usb ? "USB" : "A:");
        if (!on_usb) api->broadcast("fs.changed", name);
    } else api->kfmt(msg, sizeof msg, "screenshot failed (%s full?)", on_usb ? "USB" : "A:");
    api->notify(msg);
}

static int shot_key(int k)
{
    if (k != K_PRTSC && k != K_SYSRQ) return 0;
    shot_take(k == K_SYSRQ || (api->kbd_mods() & 8));
    return 1;
}
#pragma clang attribute pop

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, KEXT_ON_DEMAND, "shot"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    return api->register_key_hook(shot_key) != 0;
}
