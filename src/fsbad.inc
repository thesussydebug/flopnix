#pragma once

#define FB_MAX   240
#define FB_MAGIC 0x31444142u
#define FB_OFF   12

static inline int fb_parse(const u8 *sec, u16 *list, u32 first, u32 end)
{
    const u16 *w = (const u16 *)(sec + FB_OFF);
    if (*(const u32 *)w != FB_MAGIC || w[2] > FB_MAX) return 0;
    for (int i = 0; i < w[2]; i++) {
        if (w[3 + i] < first || w[3 + i] >= end || (i && w[3 + i] <= w[2 + i])) return 0;
        list[i] = w[3 + i];
    }
    return w[2];
}

static inline void fb_store(u8 *sec, const u16 *list, int n)
{
    u16 *w = (u16 *)(sec + FB_OFF);
    *(u32 *)w = FB_MAGIC;
    w[2] = (u16)n;
    for (int i = 0; i < FB_MAX; i++) w[3 + i] = i < n ? list[i] : 0;
}

static inline int fb_clash(const u16 *list, int n, u32 start, u32 len)
{
    for (int i = 0; i < n; i++)
        if (list[i] >= start && list[i] - start < len) return list[i];
    return -1;
}

static inline int fb_add(u16 *list, int *n, u32 lba)
{
    int at = 0;
    while (at < *n && list[at] < lba) at++;
    if (at < *n && list[at] == lba) return 0;
    if (*n >= FB_MAX) return -1;
    for (int i = *n; i > at; i--) list[i] = list[i - 1];
    list[at] = (u16)lba;
    ++*n;
    return 1;
}

static inline int fb_del(u16 *list, int *n, u32 lba)
{
    for (int i = 0; i < *n; i++) {
        if (list[i] != lba) continue;
        for (--*n; i < *n; i++) list[i] = list[i + 1];
        return 1;
    }
    return 0;
}
