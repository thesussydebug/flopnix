/* Session cache for decoded images, stylesheets and pages, with LRU eviction. */
#pragma once
#define BC_ENTRIES 64
enum { BC_IMAGE = 1, BC_CSS, BC_PAGE, BC_FAILED };
typedef struct { char url[TW_URL]; u8 kind; u32 size, stamp; u8 *data; int a, b; } BcEntry;
typedef struct {
    BcEntry e[BC_ENTRIES];
    u32 bytes, budget, clock, hits, misses;
    void *(*alloc)(u32);
    void (*release)(void *);
} BrCache;

static u32 bc_budget(u32 total_kb)
{
    u32 b = total_kb / 8 * 1024;
    return b < 256 * 1024 ? 256 * 1024 : b > 4096 * 1024 ? 4096 * 1024 : b;
}
static void bc_init(BrCache *c, u32 budget, void *(*alloc)(u32), void (*release)(void *))
{
    for (int i = 0; i < BC_ENTRIES; i++) { c->e[i].kind = 0; c->e[i].data = 0; c->e[i].size = 0; }
    c->bytes = c->clock = c->hits = c->misses = 0; c->budget = budget; c->alloc = alloc; c->release = release;
}
static void bc_drop(BrCache *c, BcEntry *e)
{
    if (!e->kind) return;
    if (e->data) c->release(e->data);
    c->bytes -= e->size; e->data = 0; e->size = 0; e->kind = 0;
}
static void bc_clear(BrCache *c) { for (int i = 0; i < BC_ENTRIES; i++) bc_drop(c, &c->e[i]); }
static BcEntry *bc_find(BrCache *c, const char *url, int kind)
{
    for (int i = 0; i < BC_ENTRIES; i++)
        if (c->e[i].kind == kind && tw_eq(c->e[i].url, url)) { c->e[i].stamp = ++c->clock; c->hits++; return &c->e[i]; }
    c->misses++;
    return 0;
}
static int bc_evict_one(BrCache *c)
{
    BcEntry *old = 0;
    for (int i = 0; i < BC_ENTRIES; i++) if (c->e[i].kind && (!old || (int)(c->e[i].stamp - old->stamp) < 0)) old = &c->e[i];
    if (!old) return 0;
    bc_drop(c, old);
    return 1;
}
static BcEntry *bc_put(BrCache *c, const char *url, int kind, const u8 *data, u32 size, int a, int b)
{
    int n = tw_len(url);
    if (!n || n >= TW_URL || size > c->budget / 4) return 0;
    BcEntry *slot = 0;
    for (int i = 0; i < BC_ENTRIES; i++) if (c->e[i].kind == kind && tw_eq(c->e[i].url, url)) { bc_drop(c, &c->e[i]); slot = &c->e[i]; }
    while (c->bytes + size > c->budget && bc_evict_one(c)) {}
    if (!slot) for (int i = 0; i < BC_ENTRIES && !slot; i++) if (!c->e[i].kind) slot = &c->e[i];
    if (!slot) { bc_evict_one(c); for (int i = 0; i < BC_ENTRIES && !slot; i++) if (!c->e[i].kind) slot = &c->e[i]; }
    if (!slot) return 0;
    u8 *copy = 0;
    if (size) {
        copy = c->alloc(size);
        if (!copy) return 0;
        for (u32 i = 0; i < size; i++) copy[i] = data[i];
    }
    tw_copy(slot->url, TW_URL, url, n);
    slot->kind = (u8)kind; slot->size = size; slot->data = copy; slot->a = a; slot->b = b; slot->stamp = ++c->clock;
    c->bytes += size;
    return slot;
}
