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
    u32 flags=irq_save();
    for(int pass=0;pass<2;pass++){
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
        irq_restore(flags);return b + 1;
    }
    if(!pass){fs_cache_clear();win_image_trim();}
    }
    irq_restore(flags);
    return 0;
}

void kfree(void *p)
{
    if (!heap_on || !p) return;
    u32 flags=irq_save();
    Blk *b = (Blk *)p - 1;
    if ((u32)b < HEAP_BASE || (u32)b >= heap_top) {irq_restore(flags);return;}
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
