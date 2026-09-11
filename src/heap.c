/* Allocates and releases temporary memory. */
#include "os.h"

#define HEAP_BASE (memory.heap)
#define HMAGIC    0x48454150u

typedef struct Blk {
    u32 magic;
    u32 size;
    struct Blk *next;
    u32 free;
} Blk;

static Blk *freelist;
static u32 heap_top;
static u8 heap_on;

void heap_init(void)
{
    heap_top = memory.heap_end;
    if (heap_top < HEAP_BASE + 0x8000) return;
    freelist = (Blk *)HEAP_BASE;
    freelist->magic = HMAGIC;
    freelist->size = heap_top - HEAP_BASE - sizeof(Blk);
    freelist->next = 0;
    freelist->free = 1;
    heap_on = 1;
}

void *kmalloc(u32 n)
{
    if (!heap_on || !n) return 0;

    if (n > heap_top - HEAP_BASE) return 0;
    n = (n + 7) & ~7u;
    for (Blk *b = freelist; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + sizeof(Blk) + 8) {
            Blk *rest = (Blk *)((u8 *)(b + 1) + n);
            rest->magic = HMAGIC;
            rest->size = b->size - n - sizeof(Blk);
            rest->free = 1;
            rest->next = b->next;
            b->next = rest;
            b->size = n;
        }
        b->free = 0;
        return b + 1;
    }
    return 0;
}

void kfree(void *p)
{
    if (!heap_on || !p) return;
    Blk *b = (Blk *)p - 1;
    if ((u32)b < HEAP_BASE || (u32)b >= heap_top) return;
    if (b->magic != HMAGIC || b->free) return;
    b->free = 1;
    for (Blk *s = freelist; s; s = s->next)
        while (s->free && s->next && s->next->free &&
               (u8 *)(s + 1) + s->size == (u8 *)s->next) {
            s->size += sizeof(Blk) + s->next->size;
            s->next = s->next->next;
        }
}

u32 heap_avail(void)
{
    u32 total = 0;
    if (!heap_on) return 0;
    for (Blk *b = freelist; b; b = b->next)
        if (b->free) total += b->size;
    return total;
}

u32 heap_largest(void)
{
    u32 best = 0;
    if (!heap_on) return 0;
    for (Blk *b = freelist; b; b = b->next)
        if (b->free && b->size > best) best = b->size;
    return best;
}

u32 heap_blocks(void)
{
    u32 n = 0;
    if (!heap_on) return 0;
    for (Blk *b = freelist; b; b = b->next)
        if (b->free) n++;
    return n;
}

u32 heap_base(void) { return HEAP_BASE; }
u32 heap_end(void)  { return heap_on ? heap_top : HEAP_BASE; }
