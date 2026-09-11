/* Indexes the app event queue. */
#pragma once

#define AQ_SIZE 32

static inline int aq_count(u32 head, u32 tail) { return (int)(tail - head); }
static inline int aq_full(u32 head, u32 tail)  { return aq_count(head, tail) >= AQ_SIZE; }
static inline int aq_empty(u32 head, u32 tail) { return head == tail; }
static inline int aq_slot(u32 seq)             { return (int)(seq & (AQ_SIZE - 1)); }
