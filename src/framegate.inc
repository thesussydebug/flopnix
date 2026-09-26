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

/* Every fourth half-second tick, or any tick under an overlay, repaints everything. */
static inline int tick_full(unsigned beat, int overlay)
{
    return !(beat & 3) || overlay;
}

/* Rows to copy after the cursor box moved, plus the taskbar from bar_y down when bar_y < sh. */
static inline void frame_band(int ox, int oy, int oh, int nx, int ny, int nh, int bar_y, int sh,
                              int *y0, int *y1)
{
    int a = sh, b = sh;
    if (ox != nx || oy != ny) {
        a = oy < ny ? oy : ny;
        b = oy + oh > ny + nh ? oy + oh : ny + nh;
    }
    if (bar_y < sh) {
        if (bar_y < a) a = bar_y;
        b = sh;
    }
    *y0 = a;
    *y1 = b;
}

#endif
