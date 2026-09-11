#pragma once

static inline int ax_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static inline int ax_match(const char *hay, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && ax_lower(hay[i + j]) == ax_lower(needle[j])) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static inline int ax_clamp_top(int top, int rows, int n)
{
    if (rows <= 0 || n <= 0) return 0;
    int max = n - rows;
    if (max < 0) max = 0;
    if (top > max) top = max;
    return top < 0 ? 0 : top;
}

static inline int ax_scroll(int sel, int top, int rows, int n)
{
    if (rows <= 0 || n <= 0) return 0;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    return ax_clamp_top(top, rows, n);
}
