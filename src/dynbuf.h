#pragma once
#include "buffer_core.inc"

typedef struct { void *data; u32 capacity; } DynBuf;

static inline int db_reserve(const Kapi *k, DynBuf *b, u32 count,
                             u32 width, u32 initial, u32 limit, const char *name)
{
    if (!width || limit > 0xFFFFFFFFu / width || count > limit) return 0;
    if (count <= b->capacity) return 1;
    u32 cap = buffer_capacity(b->capacity, count, initial, limit);
    if (!cap) return 0;
    void *p = k->krealloc(b->data, cap * width);
    if (!p && cap > count) { cap = count; p = k->krealloc(b->data, cap * width); }
    if (!p) return 0;
    b->data = p; b->capacity = cap;
    k->mem_track(name, p, cap * width);
    return 1;
}

static inline void db_trim(const Kapi *k, DynBuf *b, u32 count, u32 width)
{
    if (!count) { k->kfree(b->data); b->data = 0; b->capacity = 0; return; }
    if (!width || count >= b->capacity || count > 0xFFFFFFFFu / width) return;
    void *p = k->krealloc(b->data, count * width);
    if (p) { b->data = p; b->capacity = count; }
}

static inline void db_free(const Kapi *k, DynBuf *b) { db_trim(k, b, 0, 1); }
