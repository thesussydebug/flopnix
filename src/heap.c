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
static u32 grow_base, grow_top, grow_limit;
static u8 heap_on;
static MemBuffer buffers[32];

void mem_track(const char *name, const void *ptr, u32 size)
{
    if (!name || !ptr) return;
    if(!size){mem_untrack(ptr);return;}
    u32 flags=irq_save();
    int slot=-1;
    for(int i=0;i<32;i++){if(buffers[i].size&&buffers[i].base==(u32)ptr){slot=i;break;}if(!buffers[i].size)slot=i;}
    if(slot>=0){int i=slot;
        strlcpy(buffers[i].name,name,sizeof buffers[i].name);
        buffers[i].base=(u32)ptr;buffers[i].size=size;buffers[i].owner=kext_owner_now();irq_restore(flags);return;
    }
    irq_restore(flags);
}
void mem_untrack(const void *ptr)
{
    u32 flags=irq_save();
    for(int i=0;i<32;i++)if(buffers[i].base==(u32)ptr)buffers[i].size=0;
    irq_restore(flags);
}
int mem_buffer(int index, MemBuffer *out)
{
    if(index<0||!out)return 0;
    u32 flags=irq_save();MemBuffer value;int found=0;
    for(int i=0;i<32;i++)if(buffers[i].size&&index--==0){value=buffers[i];found=1;break;}
    irq_restore(flags);if(found)*out=value;return found;
}

void heap_init(void)
{
    heap_top = memory.heap_end;
    grow_base = grow_top = memory.pool_end > heap_top ? memory.pool_end : heap_top;
    grow_limit = memory_heap_limit(BOOTINFO->mem_kb, BOOTINFO->lfb);
    if (grow_limit < grow_base) grow_limit = grow_base;
    if (heap_top < HEAP_BASE + 0x8000) return;
    freelist = (Blk *)HEAP_BASE;
    freelist->magic = HMAGIC;
    freelist->size = heap_top - HEAP_BASE - sizeof(Blk);
    freelist->next = 0;
    freelist->free = 1;
    heap_on = 1;
}

static void split(Blk *b, u32 n)
{
    if (b->size < n + sizeof(Blk) + 8) return;
    Blk *rest = (Blk *)((u8 *)(b + 1) + n);
    rest->magic = HMAGIC;
    rest->size = b->size - n - sizeof(Blk);
    rest->free = 1;
    rest->next = b->next;
    while (rest->next && rest->next->free &&
           (u8 *)(rest + 1) + rest->size == (u8 *)rest->next) {
        rest->size += sizeof(Blk) + rest->next->size;
        rest->next = rest->next->next;
    }
    b->next = rest;
    b->size = n;
}

static int heap_grow(u32 n)
{
    Blk *last = freelist;
    while (last->next) last = last->next;
    int extend = last->free && (u32)(last + 1) + last->size == grow_top;
    u32 need = extend ? n - last->size : n + sizeof(Blk);
    u32 room = grow_limit - grow_top;
    if (need > room) return 0;
    u32 bytes = (need + 4095u) & ~4095u;
    if (bytes < 65536u) bytes = 65536u;
    if (bytes > room) bytes = room;
    if (extend) last->size += bytes;
    else {
        Blk *b = (Blk *)grow_top;
        b->magic = HMAGIC; b->size = bytes - sizeof(Blk);
        b->next = 0; b->free = 1; last->next = b;
    }
    grow_top += bytes;
    return 1;
}

static int heap_contains(u32 p)
{
    return !(p & 7u) && ((p >= HEAP_BASE && p <= heap_top - sizeof(Blk)) ||
        (p >= grow_base && p < grow_top && p <= grow_top - sizeof(Blk)));
}

void *kmalloc(u32 n)
{
    if (!heap_on || !n) return 0;

    if (n > heap_top - HEAP_BASE + grow_limit - grow_base) return 0;
    n = (n + 7) & ~7u;
    u32 flags=irq_save();
    for(int pass=0;pass<3;pass++){
    for (Blk *b = freelist; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        split(b, n);
        b->free = 0;
        irq_restore(flags);return b + 1;
    }
    if(pass<2 && heap_grow(n)) continue;
    if(pass<2){fs_cache_clear();win_image_trim();}
    }
    irq_restore(flags);
    return 0;
}

void *krealloc(void *p, u32 n)
{
    if (!p) return kmalloc(n);
    if (!n) { kfree(p); return 0; }
    if (!heap_on || n > heap_top - HEAP_BASE + grow_limit - grow_base) return 0;
    u32 size = (n + 7) & ~7u, flags = irq_save();
    Blk *b = (Blk *)p - 1;
    if (!heap_contains((u32)b) ||
        b->magic != HMAGIC || b->free) { irq_restore(flags); return 0; }
    if (b->size < size && b->next && b->next->free &&
        (u8 *)(b + 1) + b->size == (u8 *)b->next &&
        b->size + sizeof(Blk) + b->next->size >= size) {
        b->size += sizeof(Blk) + b->next->size;
        b->next = b->next->next;
    }
    void *next = p;
    if (b->size >= size) split(b, size);
    else {
        next = kmalloc(n);
        if (!next) { irq_restore(flags); return 0; }
        memcpy(next, p, b->size);
    }
    for (int i = 0; i < 32; i++) if (buffers[i].size && buffers[i].base == (u32)p) {
        buffers[i].base = (u32)next;
        buffers[i].size = n;
    }
    if (next != p) kfree(p);
    irq_restore(flags);
    return next;
}

void kfree(void *p)
{
    if (!heap_on || !p) return;
    u32 flags=irq_save();
    Blk *b = (Blk *)p - 1;
    if (!heap_contains((u32)b)) {irq_restore(flags);return;}
    if (b->magic != HMAGIC || b->free) {irq_restore(flags);return;}
    mem_untrack(p);
    b->free = 1;
    for (Blk *s = freelist; s; s = s->next)
        while (s->free && s->next && s->next->free &&
               (u8 *)(s + 1) + s->size == (u8 *)s->next) {
            s->size += sizeof(Blk) + s->next->size;
            s->next = s->next->next;
        }
    irq_restore(flags);
}

u32 heap_avail(void)
{
    u32 total = 0;
    if (!heap_on) return 0;
    u32 flags=irq_save();
    for (Blk *b = freelist; b; b = b->next)
        if (b->free) total += b->size;
    irq_restore(flags);return total;
}

u32 heap_largest(void)
{
    u32 best = 0;
    if (!heap_on) return 0;
    u32 flags=irq_save();
    for (Blk *b = freelist; b; b = b->next)
        if (b->free && b->size > best) best = b->size;
    irq_restore(flags);return best;
}

u32 heap_blocks(void)
{
    u32 n = 0;
    if (!heap_on) return 0;
    u32 flags=irq_save();
    for (Blk *b = freelist; b; b = b->next)
        if (b->free) n++;
    irq_restore(flags);return n;
}

u32 heap_base(void) { return HEAP_BASE; }
u32 heap_end(void)  { return heap_on ? heap_top : HEAP_BASE; }
u32 heap_grow_base(void) { return grow_base; }
u32 heap_grow_end(void) { return grow_top; }
u32 heap_capacity(void) { return heap_end() - HEAP_BASE + grow_top - grow_base; }
u32 heap_limit(void) { return heap_end() - HEAP_BASE + grow_limit - grow_base; }
