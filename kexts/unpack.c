#include "kapi.h"
#include "kextpack.h"

#pragma clang attribute push(__attribute__((minsize)), apply_to = function)
#include "gunzip.inc"

static const Kapi *api;

static int pk_open(const u8 *data, u32 length, u8 **image)
{
    Gunzip g;
    u32 full = kp_size(data, length);
    if (!full) return 0;
    if (full > api->iobuf_size) return -40;
    u8 *raw = api->kmalloc(full);
    if (!raw) return -42;
    if (gz_unpack(&g, data, length, raw, full) != (int)full) {
        api->kfree(raw);
        return -40;
    }
    *image = raw;
    return (int)full;
}

static int pk_head(const char *name, u8 *head)
{
    return api->fs_read(name, head, KP_HEAD) == (int)KP_HEAD ? (int)kp_size(head, KP_HEAD) : 0;
}

static int pk_kind(const char *name)
{
    u8 head[KP_HEAD];
    return pk_head(name, head) ? head[20] : -1;
}

static int pk_load(const char *name, u8 **image)
{
    Gunzip g;
    u8 head[KP_HEAD];
    u32 full = (u32)pk_head(name, head), cap = full + KP_SLACK;
    if (!full) return 0;
    if (full > api->iobuf_size - KP_SLACK) return -40;
    u8 *raw = *image ? *image : api->kmalloc(cap);
    if (!raw) return -42;
    int size = api->fs_read(name, raw, cap);
    u8 *src = raw + cap - size;
    if (size > (int)KP_HEAD) api->memmove(src, raw, (u32)size);
    if (size <= (int)KP_HEAD || gz_unpack(&g, src, (u32)size, raw, full) != (int)full) {
        if (raw != *image) api->kfree(raw);
        return -40;
    }
    *image = raw;
    return (int)full;
}

static int pk_gunzip(const u8 *data, u32 length, u8 **out)
{
    Gunzip g;
    if (length < 18) return -40;
    const u8 *t = data + length - 4;
    u32 full = (u32)t[0] | (u32)t[1] << 8 | (u32)t[2] << 16 | (u32)t[3] << 24;
    if (full > 0x3FFFFFFFu) return -40;
    u8 *raw = api->kmalloc(full + 1);
    if (!raw) return -42;
    if (gz_unpack(&g, data, length, raw, full + 1) != (int)full) {
        api->kfree(raw);
        return -40;
    }
    *out = raw;
    return (int)full;
}
#pragma clang attribute pop

static const KextPackOps pack_ops = { KEXT_PACK_ABI, pk_load, pk_open, pk_kind, pk_gunzip };

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "unpack"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    return api->register_service("kext.pack", &pack_ops) < 0;
}
