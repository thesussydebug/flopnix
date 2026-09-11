/* Calculates the scrollbar thumb size and position. */
#pragma once

static inline void sb_thumb(int len, int total, int vis, int off,
                            int *tl, int *tp)
{
    if (total <= vis || total <= 0) { *tl = len; *tp = 0; return; }
    *tl = len * vis / total;
    if (*tl < 16) *tl = 16;
    if (*tl > len) *tl = len;

    int span = total - vis;
    if (off > span) off = span;
    if (off < 0) off = 0;
    *tp = (len - *tl) * off / span;
}
