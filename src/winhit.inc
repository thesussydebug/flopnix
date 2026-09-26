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

#define BORDER 3
#define TITLEH 18

/* Returns what a pointer at (px, py) highlights, or -1 inside a client area. */
static int wh_hover(const int *zord, int nz, const WhRect *rects, int px, int py,
                    int bar_y, int bw, int nbtn, int limit)
{
    int code = 1;
    if (py >= bar_y) {
        int d = px - 64, s = bw + 4;
        if ((unsigned)(py - bar_y - 3) < 22u) {
            if ((unsigned)(px - 2) < 56u) code = 2;
            else if (d >= 0 && d % s < bw && d / s < nbtn && px - d % s + bw <= limit) code = 3 + d / s;
        }
        return code << 16;
    }
    const WhRect *t = 0;
    code = 0;
    for (int k = 0; k < nz; k++) {
        int i = zord[k];
        const WhRect *r = &rects[i];
        if ((unsigned)(px - r->x) < (unsigned)r->w && (unsigned)(py - r->y) < (unsigned)r->h) {
            t = r;
            code = (code & ~15) | (i + 1);
        }
        if ((unsigned)(px - r->x - r->w + BORDER + 17) < 16u && (unsigned)(py - r->y - BORDER - 1) < 16u)
            code |= 16 << i;
    }
    if (t && (unsigned)(px - t->x - BORDER) < (unsigned)(t->w - 2 * BORDER) &&
        (unsigned)(py - t->y - BORDER - TITLEH - 1) < (unsigned)(t->h - 2 * BORDER - TITLEH - 1))
        return -1;
    return code;
}

static inline int wh_covered(const int *zord,int nz,const WhRect *rects,int k,int width,int height)
{
    const WhRect *r=&rects[zord[k]];
    int x0=r->x<0?0:r->x,y0=r->y<0?0:r->y;
    int x1=r->x+r->w,y1=r->y+r->h;
    if(x1>width)x1=width;if(y1>height)y1=height;
    if(x0>=x1||y0>=y1)return 1;
    for(int j=k+1;j<nz;j++){
        const WhRect *a=&rects[zord[j]];
        if(a->w>0&&a->h>0&&a->x<=x0&&a->y<=y0&&a->x+a->w>=x1&&a->y+a->h>=y1)return 1;
    }
    return 0;
}
