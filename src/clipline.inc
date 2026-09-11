/* Pastes clipboard text into a single-line field. */
#pragma once

static inline int cl_paste(char *dst, int len, int cap, const char *src)
{
    if (len < 0 || len >= cap) return len;
    while (*src && len < cap - 1) {
        char c = *src++;
        if (c < 32 || c > 126) break;
        dst[len++] = c;
    }
    dst[len] = 0;
    return len;
}
