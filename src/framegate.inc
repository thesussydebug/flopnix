/* Tracks frame presentation timing. */
#ifndef FRAMEGATE_INC
#define FRAMEGATE_INC

typedef struct {
    int  dirty;
    int  mx, my;
    int  blink;
    int  ss;
} FrameState;

static inline int frame_present(const FrameState *now, const FrameState *last)
{
    if (now->dirty || now->ss) return 1;
    return now->mx != last->mx || now->my != last->my ||
           now->blink != last->blink;
}

#endif
