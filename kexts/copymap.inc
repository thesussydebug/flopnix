#pragma once
#define CM_SECTORS 2880u
#define CM_SYSTEM 369u

static u32 cm_build(u8 *map, FsEnt *(*slot)(int i), int slots)
{
    u32 used = 0;
    for (u32 i = 0; i < CM_SECTORS; i++) map[i] = (u8)(!slot || i < CM_SYSTEM);
    for (int i = 0; slot && i < slots; i++) {
        const FsEnt *e = slot(i);
        if (!e) break;
        if (!e->used) continue;
        for (u32 s = e->start, n = e->nsect; n && s < CM_SECTORS; s++, n--) map[s] = 1;
    }
    for (u32 i = 0; i < CM_SECTORS; i++) used += map[i];
    return used;
}

static u32 cm_run(const u8 *map, u32 *at, u32 max)
{
    u32 a = *at, n = 0;
    while (a < CM_SECTORS && !map[a]) a++;
    while (a + n < CM_SECTORS && n < max && map[a + n]) n++;
    if (n) *at = a;
    return n;
}

static u32 cm_index(const u8 *map, u32 lba)
{
    u32 n = 0;
    for (u32 i = 0; i < lba && i < CM_SECTORS; i++) n += map[i];
    return n;
}
