/* Chooses physical memory reservations for the available RAM. */
typedef struct {
    u32 fb, fb_end, arena, arena_end, pool, pool_end, heap, heap_end;
} MemoryLayout;
#define MEM_IO_BASE 0x270000u
#define MEM_IO_END  0x3B4000u
#define MEM_DMA_BASE 0x80000u
#define MEM_STACK_TOP 0x9E000u
static inline u32 memory_heap_limit(u32 kb, u32 lfb)
{
    u32 end = kb >= 0x100000u ? 0x40000000u : kb * 1024u;
    if (lfb >= 0x400000u && lfb < end) end = lfb & ~0x3FFFFFu;
    return end & ~4095u;
}
static inline MemoryLayout memory_layout(u32 kb, u32 screen)
{
    MemoryLayout m = {MEM_IO_END, 0, MEM_IO_BASE - 0x100000u, MEM_IO_BASE,
                      0, 0, 0x30000u, MEM_DMA_BASE};
    if (kb >= 9216) {
        if (screen < 640u * 480u) screen = 640u * 480u;
        if (screen > 0x100000u) screen = 0x100000u;
        m.fb_end = m.pool = m.fb + ((screen + 4095u) & ~4095u);
        if (m.pool >> 22 != (m.pool + 0xFFFFFu) >> 22) m.pool = (m.pool + 0x3FFFFFu) & ~0x3FFFFFu;
        m.pool_end = m.pool + 0x100000u;
        return m;
    }
    m.fb = 0x170000u; m.fb_end = m.arena = 0x1C0000u;
    m.pool = 0x30000u; m.pool_end = MEM_DMA_BASE;
    m.heap = MEM_IO_END; m.heap_end = kb ? kb * 1024 : 0x400000u;
    if (kb >= 5120) {
        m.arena = MEM_IO_BASE - 0x100000u;
        m.fb = MEM_IO_END; m.fb_end = m.heap = MEM_IO_END + 0x50000u;
    }
    if (m.heap_end < m.heap) m.heap_end = m.heap;
    return m;
}
