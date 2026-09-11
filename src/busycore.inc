/* Tracks how long an event handler has been running. */
#pragma once
#define BC_SHOW 25
typedef struct {
    int win;
    u32 since;
    u8 running;
} BusyCore;
static inline void bc_start(BusyCore *b, int win, u32 now)
{
    b->win = win; b->since = now; b->running = 1;
}
static inline void bc_end(BusyCore *b, u32 now)
{
    (void)now; b->running = 0;
}
static inline int bc_show(BusyCore *b, int win, u32 now)
{
    return b->running && b->win == win && (u32)(now - b->since) >= BC_SHOW;
}
