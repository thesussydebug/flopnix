/* Checks whether the next frame is due. */
#define FLIP_TICKS 2

static inline int flip_due(u32 now, u32 *last, int alive)
{
    if (alive && (u32)(now - *last) < FLIP_TICKS) return 0;
    *last = now;
    return 1;
}
