/* Tracks CPU ownership across nested callbacks. */
#ifndef CA_APPS
#define CA_APPS 40
#define CA_THREADS 8
#endif

static inline u32 ca_stamp(u32 lo, u32 hi) { return (lo >> 10) | (hi << 22); }
typedef struct {
    u32 acc[CA_APPS], last;
    int owner[CA_THREADS], thread;
} CpuAccount;
static inline void ca_step(CpuAccount *c, u32 now) {
    int owner = c->owner[c->thread];
    if (owner >= 0 && owner < CA_APPS) c->acc[owner] += now - c->last;
    c->last = now;
}
static inline int ca_owner(CpuAccount *c, u32 now, int owner) {
    ca_step(c, now); int old = c->owner[c->thread]; c->owner[c->thread] = owner; return old;
}
static inline void ca_switch(CpuAccount *c, u32 now, int thread) {
    ca_step(c, now); c->thread = thread;
}
