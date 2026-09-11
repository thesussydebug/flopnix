/* Checks when the trace log can be written to USB. */
#define FLIGHT_TICKS   1500u
#define FLIGHT_BACKOFF 4500u

static inline int flight_due(u32 now, u32 last, u32 backoff,
                             int dirty, int gui_busy)
{
    if (!dirty) return 0;
    u32 due = FLIGHT_TICKS + backoff;
    u32 el = now - last;
    if (el < due) return 0;
    if (gui_busy && el < 2 * due) return 0;
    return 1;
}
