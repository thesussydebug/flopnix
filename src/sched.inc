/* Chooses the next runnable thread. */
#pragma once

enum {
    THR_FREE = 0,
    THR_READY,
    THR_RUN,
    THR_BLOCKED,
    THR_DEAD
};

static inline int thr_may_switch(int on, int armed, int depth, int irqnest)
{
    (void)armed;
    return on && depth == 0 && irqnest <= 1;
}

static inline int sched_next(const unsigned char *state, int n, int cur)
{
    if (n <= 0) return -1;
    for (int i = 1; i <= n; i++) {
        int s = (cur + i) % n;
        if (s < 0) s += n;
        if (state[s] == THR_READY || state[s] == THR_RUN) return s;
    }
    return -1;
}
