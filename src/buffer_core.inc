#pragma once
static inline u32 buffer_capacity(u32 current, u32 needed, u32 initial, u32 limit)
{
    if (needed > limit || current > limit || !initial) return 0;
    u32 cap = current ? current : (initial < limit ? initial : limit);
    while (cap < needed) cap = cap > limit / 2 ? limit : cap * 2;
    return cap;
}
