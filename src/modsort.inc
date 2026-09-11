/* Orders loaded extensions by memory use. */
#pragma once

static inline int mod_sort(const u32 *sz, int n, int cap, int *idx)
{
    if (n < 0) n = 0;
    if (cap < 0) cap = 0;
    for (int i = 0; i < n; i++) {
        int rank = 0;
        for (int j = 0; j < n; j++)
            if (sz[j] > sz[i] || (sz[j] == sz[i] && j < i)) rank++;
        if (rank < cap) idx[rank] = i;
    }
    return n < cap ? n : cap;
}

static inline int mod_share(int bw, u32 size, u32 big)
{
    if (!big || bw <= 0) return 0;
    if (size >= big) return bw;
    return (int)((u32)bw * size / big);
}
