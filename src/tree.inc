/* Formats folder-tree lines. */
#pragma once

static inline int tr_depth(const char *name)
{
    if (!name) return 0;
    int d = 0;
    for (int i = 0; name[i]; i++)
        if (name[i] == '/') d++;
    return d;
}

static inline const char *tr_leaf(const char *name)
{
    if (!name) return 0;
    const char *l = name;
    for (int i = 0; name[i]; i++)
        if (name[i] == '/') l = name + i + 1;
    return l;
}

static inline int tr_prefix(const unsigned char *last_flags, int depth,
                            int is_last, char *out, int cap)
{
    if (!out || cap < 1) return 0;
    int o = 0;
    for (int d = 0; d < depth; d++) {
        const char *seg = (last_flags && last_flags[d]) ? "   " : "|  ";
        for (int i = 0; i < 3; i++)
            if (o < cap - 1) out[o++] = seg[i];
    }
    const char *tip = is_last ? "`- " : "|- ";
    for (int i = 0; i < 3; i++)
        if (o < cap - 1) out[o++] = tip[i];
    out[o] = 0;
    return o;
}
