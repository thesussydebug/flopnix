/* Chooses physical memory reservations for the available RAM. */
typedef struct {
    u32 fb, fb_end, arena, arena_end, pool, pool_end, heap, heap_end;
} MemoryLayout;
#define MEM_IO_BASE 0x270000u
#define MEM_IO_END  0x3B4000u
#define MEM_DMA_BASE 0x80000u
#define MEM_STACK_TOP 0x9E000u
static inline MemoryLayout memory_layout(u32 kb)
{
    MemoryLayout m = {0x400000u, 0x500000u, 0x600000u, 0x700000u,
                      0x800000u, 0x900000u, 0x700000u, 0x800000u};
    if (kb < 9216) {
        m.fb = 0x190000u; m.fb_end = 0x1E0000u;
        m.arena = 0x1E0000u; m.arena_end = MEM_IO_BASE;
        m.pool = 0x30000u; m.pool_end = MEM_DMA_BASE;
        m.heap = MEM_IO_END; m.heap_end = 0x400000u;
    }
    if (kb && kb < m.heap_end / 1024) m.heap_end = kb * 1024;
    if (m.heap_end < m.heap) m.heap_end = m.heap;
    return m;
}
