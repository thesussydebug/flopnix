/* Fits a window within the screen. */
typedef struct { int x, y, w, h; } WinFit;

static inline WinFit win_fit(int x, int y, int w, int h,
                             int avail_w, int avail_h)
{
    WinFit f;
    f.w = w > avail_w ? avail_w : w;
    f.h = h > avail_h ? avail_h : h;
    f.x = x;
    f.y = y;
    if (f.x + f.w > avail_w) f.x = avail_w - f.w;
    if (f.y + f.h > avail_h) f.y = avail_h - f.h;
    if (f.x < 0) f.x = 0;

    if (f.y < 0) f.y = 0;
    return f;
}
