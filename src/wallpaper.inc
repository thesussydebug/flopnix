/* Checks wallpaper settings and placement. */
#pragma once

enum { WP_GRADIENT = 0, WP_SOLID = 1, WP_BITMAP = 2 };

static inline u8 wp_mode_norm(u8 mode, const char *path)
{
    if (mode == WP_SOLID) return WP_SOLID;
    if (mode == WP_BITMAP && path && path[0]) return WP_BITMAP;
    return WP_GRADIENT;
}

static inline int wp_path_fix(char *p, int cap)
{
    if (!p || cap <= 0) return 0;
    p[cap - 1] = 0;
    return p[0] != 0;
}

static inline int wp_rgb_any(const u8 *p, int n)
{
    if (!p) return 0;
    for (int i = 0; i < n; i++) if (p[i]) return 1;
    return 0;
}
