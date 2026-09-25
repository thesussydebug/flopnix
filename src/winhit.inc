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
