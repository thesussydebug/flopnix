/* Handles scrollbar clicks and dragging. */
#pragma once

#include "sbar.inc"

enum { SBZ_PAGEUP, SBZ_THUMB, SBZ_PAGEDN };

typedef struct {
    u8  active;
    int grab_pos;
    int grab_off;
} SbDrag;

static inline int sb_clamp(int off, int total, int vis)
{
    int span = total - vis;
    if (span < 0) span = 0;
    if (off > span) off = span;
    if (off < 0) off = 0;
    return off;
}

static inline int sb_zone(int len, int total, int vis, int off, int pos)
{
    int tl, tp;
    sb_thumb(len, total, vis, off, &tl, &tp);
    if (pos < tp) return SBZ_PAGEUP;
    if (pos < tp + tl) return SBZ_THUMB;
    return SBZ_PAGEDN;
}

static inline int sb_press(SbDrag *d, int len, int total, int vis, int off,
                           int pos)
{
    int z = sb_zone(len, total, vis, off, pos);
    d->active = 0;
    if (z == SBZ_THUMB) {

        d->active = 1;
        d->grab_pos = pos;
        d->grab_off = sb_clamp(off, total, vis);
        return d->grab_off;
    }
    return sb_clamp(off + (z == SBZ_PAGEUP ? -vis : vis), total, vis);
}

static inline int sb_move(const SbDrag *d, int len, int total, int vis, int pos)
{
    int span = total - vis;
    if (span <= 0) return 0;
    int tl, tp;
    sb_thumb(len, total, vis, d->grab_off, &tl, &tp);
    int travel = len - tl;
    if (travel <= 0) return sb_clamp(d->grab_off, total, vis);

    int dp = pos - d->grab_pos;
    int delta = (dp * span + (dp >= 0 ? travel / 2 : -travel / 2)) / travel;
    return sb_clamp(d->grab_off + delta, total, vis);
}
