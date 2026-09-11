typedef struct {
    int tiny, narrow, activity_y, activity_h, surface_y, buttons_y;
    int map_x, map_y, map_w, map_h, ticks_y, legend_y, seek_y, progress_y, status_y;
} FhLayout;
static inline FhLayout fh_layout(int w, int h)
{
    FhLayout l;
    l.tiny = h < 300;
    l.narrow = w < 500;
    l.status_y = h - 20;
    l.activity_y = 38;
    l.activity_h = h < 370 ? 44 : 84;
    l.surface_y = l.tiny ? 30 : l.activity_y + l.activity_h + 12;
    l.buttons_y = l.surface_y + (l.tiny ? 2 : 12);
    l.map_x = 20;
    l.map_w = w - 40;
    l.map_y = l.buttons_y + (l.tiny ? 28 : 42);
    int legend_h = l.narrow ? 36 : 18;
    int below = l.tiny ? legend_h + 8 : 20 + legend_h + 22 + 24 + 12;
    l.map_h = l.status_y - l.map_y - below;
    if (l.map_h > 100) l.map_h = 100;
    if (l.map_h < 4) l.map_h = 4;
    l.ticks_y = l.map_y + l.map_h + 2;
    l.legend_y = l.map_y + l.map_h + (l.tiny ? 4 : 22);
    l.seek_y = l.legend_y + legend_h + 2;
    l.progress_y = l.seek_y + 22;
    return l;
}
