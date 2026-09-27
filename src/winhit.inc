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
#define WH_CLIENT (1 << 24)
#define wh_win(code) (((code) & 15) - 1)
#define wh_bar(code) (((code) >> 16) & 255)

/* Returns what a pointer at (px, py) highlights; WH_CLIENT marks the top window's client area. */
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
        code |= WH_CLIENT;
    return code;
}

/* Splits window zord[k] into on-screen pieces no higher window covers; -1 if more than max. */
__attribute__((minsize)) static int wr_visible(const int *zord, int nz, const WhRect *rects, int k, int sw, int sh,
                      WhRect *out, int max)
{
    const WhRect *r = &rects[zord[k]];
    int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w < sw ? r->x + r->w : sw, y1 = r->y + r->h < sh ? r->y + r->h : sh;
    if (x0 >= x1 || y0 >= y1) return 0;
    out[0] = (WhRect){x0, y0, x1 - x0, y1 - y0};
    int n = 1;
    for (int j = k + 1; j < nz; j++) {
        const WhRect *a = &rects[zord[j]];
        int ax1 = a->x + a->w, ay1 = a->y + a->h;
        for (int p = n; p-- > 0;) {
            WhRect q = out[p];
            int qx1 = q.x + q.w, qy1 = q.y + q.h;
            if (a->x >= qx1 || ax1 <= q.x || a->y >= qy1 || ay1 <= q.y) continue;
            if (n + 3 > max) return -1;
            out[p] = out[--n];
            int top = a->y > q.y ? a->y : q.y, bot = ay1 < qy1 ? ay1 : qy1;
            if (a->y > q.y) out[n++] = (WhRect){q.x, q.y, q.w, a->y - q.y};
            if (ay1 < qy1) out[n++] = (WhRect){q.x, ay1, q.w, qy1 - ay1};
            if (a->x > q.x) out[n++] = (WhRect){q.x, top, a->x - q.x, bot - top};
            if (ax1 < qx1) out[n++] = (WhRect){ax1, top, qx1 - ax1, bot - top};
        }
    }
    return n;
}

/* Nonzero when moving from (ox, oy) to (nx, ny) enters or leaves any of the n rects. */
static int hv_crossed(const WhRect *r, int n, int ox, int oy, int nx, int ny)
{
    for (int i = 0; i < n; i++, r++)
        if (((unsigned)(ox - r->x) < (unsigned)r->w && (unsigned)(oy - r->y) < (unsigned)r->h) !=
            ((unsigned)(nx - r->x) < (unsigned)r->w && (unsigned)(ny - r->y) < (unsigned)r->h))
            return 1;
    return 0;
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
