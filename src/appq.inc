/* Indexes the app event queue. */
#pragma once

#define AQ_SIZE 32

static inline int aq_count(u32 head, u32 tail) { return (int)(tail - head); }
static inline int aq_full(u32 head, u32 tail)  { return aq_count(head, tail) >= AQ_SIZE; }
static inline int aq_empty(u32 head, u32 tail) { return head == tail; }
static inline int aq_slot(u32 seq)             { return (int)(seq & (AQ_SIZE - 1)); }

#define AQ_WORK 0xFF

static inline int aq_conflict(int type, int owner, int legacy, int jtype, int jowner, int jlegacy)
{
    return (type >= 0 && jtype == type) || (owner >= 0 && jowner == owner) || (legacy && jlegacy);
}
