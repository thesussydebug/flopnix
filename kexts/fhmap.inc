#define FH_BELOW_MAP (4 * 18 + 2)

static inline int fh_cellw(int box_w, int ncyl)
{
    int w = (box_w - 4) / ncyl;
    if (w < 3) w = 3;
    if (w > 12) w = 12;
    return w;
}

static inline int fh_cellh(int avail_h, int nheads)
{
    int room = avail_h - FH_BELOW_MAP;
    int h = room / nheads;
    if (h < 8) h = 8;
    if (h > 40) h = 40;
    return h;
}
