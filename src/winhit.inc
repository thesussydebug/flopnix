/* Finds the topmost window at a screen position. */
typedef struct { int x, y, w, h; } WhRect;

static inline int wh_top_at(const int *zord, int nz, const WhRect *rects,
                            int px, int py)
{
    for (int k = nz - 1; k >= 0; k--) {
        int i = zord[k];
        const WhRect *r = &rects[i];
        if (px >= r->x && px < r->x + r->w &&
            py >= r->y && py < r->y + r->h) return i;
    }
    return -1;
}
