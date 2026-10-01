/* Image Viewer helpers: picture file names, folder neighbours, zoom steps and view geometry. */
#pragma once

static inline int nav_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static inline int nav_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = nav_lower((u8)*a), y = nav_lower((u8)*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
}

static inline int nav_is_picture(const char *name)
{
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return 0;
    static const char *ext[] = { "png", "jpg", "jpeg", "gif", "bmp" };
    for (unsigned i = 0; i < sizeof ext / sizeof *ext; i++)
        if (!nav_cmp(dot + 1, ext[i])) return 1;
    return 0;
}

/* Offers one candidate name; best keeps the nearest name after (dir > 0) or before cur,
   wrap keeps the first (dir > 0) or last name overall for wrapping around. */
static inline void nav_offer(const char *cur, const char *cand, int dir,
                             const char **best, const char **wrap)
{
    int c = nav_cmp(cand, cur);
    if (!c) return;
    if (dir > 0 ? c > 0 : c < 0) {
        if (!*best || (dir > 0 ? nav_cmp(cand, *best) < 0 : nav_cmp(cand, *best) > 0)) *best = cand;
    }
    if (!*wrap || (dir > 0 ? nav_cmp(cand, *wrap) < 0 : nav_cmp(cand, *wrap) > 0)) *wrap = cand;
}

static const u16 nav_zooms[] = { 10, 25, 50, 75, 100, 150, 200, 300, 400, 800 };
#define NAV_NZOOM ((int)(sizeof nav_zooms / sizeof nav_zooms[0]))

static inline int nav_zoom_step(int zoom, int dir)
{
    if (dir > 0) {
        for (int i = 0; i < NAV_NZOOM; i++) if (nav_zooms[i] > zoom) return nav_zooms[i];
        return nav_zooms[NAV_NZOOM - 1];
    }
    for (int i = NAV_NZOOM - 1; i >= 0; i--) if (nav_zooms[i] < zoom) return nav_zooms[i];
    return nav_zooms[0];
}

/* Displayed size of an ow x oh picture in an aw x ah area: fitted pictures only shrink. */
static inline void nav_display(int ow, int oh, int aw, int ah, int fit, int zoom, int *dw, int *dh)
{
    if (fit) {
        if (ow <= aw && oh <= ah) { *dw = ow; *dh = oh; return; }
        img_fit(ow, oh, aw, ah, dw, dh);
        return;
    }
    *dw = (int)((u32)ow * (u32)zoom / 100u);
    *dh = (int)((u32)oh * (u32)zoom / 100u);
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
}

static inline int nav_clamp(int off, int total, int vis)
{
    if (off > total - vis) off = total - vis;
    return off < 0 ? 0 : off;
}

/* Zoom that shows the fitted picture at the same size, as a starting point for +/-. */
static inline int nav_fit_zoom(int ow, int dw)
{
    int z = ow > 0 ? (int)((u32)dw * 100u / (u32)ow) : 100;
    return z < 1 ? 1 : z;
}
