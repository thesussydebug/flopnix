/* Checks when an app has stopped responding. */
#pragma once

#define HW_WAIT 300
#define HW_HARD 6000

typedef struct {
    int win;
    u32 next;
    u32 hard;
    u32 prog;
    u8  prompted;
} HangWatch;

static inline void hw_io(HangWatch *h,int w,u32 elapsed)
{
    if(w>=0 && h->win==w) h->hard=elapsed+HW_HARD;
}

static inline int hw_tick(HangWatch *h, int w, u32 elapsed, u32 prog)
{
    if (w < 0) { h->win = -1; return 0; }
    if (w != h->win) {
        h->win = w;
        h->next = HW_WAIT;
        h->hard = HW_HARD;
        h->prog = prog;
        h->prompted = 0;
    } else if (prog != h->prog) {
        h->prog = prog;

        if (elapsed + HW_WAIT > h->next) h->next = elapsed + HW_WAIT;
    }
    if (h->prompted) return 0;
    if (elapsed >= h->next || elapsed >= h->hard) { h->prompted = 1; return 1; }
    return 0;
}

static inline void hw_rearm(HangWatch *h, u32 elapsed)
{
    h->next = elapsed + HW_WAIT;
    h->hard = elapsed + HW_HARD;
    h->prompted = 0;
}

static inline int hw_flagged(const HangWatch *h, int w)
{
    return h->win == w && h->prompted;
}
