typedef struct {
    int s, scale, pitch, cx, text_w;
    int title_y, icon_x, icon_y, stop_y, line_y, err_y, foot_y, rep_y;
    int bar_x, bar_y, bar_w, bar_h, border;
    int info_cx, info_y, info_w;
    int arc_x, arc_y, arc_r;
} PanicLayout;

static __attribute__((unused)) void ps_layout(PanicLayout *l, int sw, int sh, int dump)
{
    int compact = sw < 640 || sh < 400;
    int s = compact ? 0 : sh / 192, sp = !compact, gap = sp * (4 * s + 4);
    l->s = s;
    l->scale = compact ? 1 : s > 4 ? s / 2 : 2;
    l->pitch = compact ? 16 : 18;
    int h = 16 * l->scale + 2 * gap + 40 * s + 32 + 9 * l->pitch + 36 * sp;
    l->title_y = (sh - h) / 2;
    l->icon_y = l->title_y + 16 * l->scale + gap;
    l->stop_y = l->icon_y + 40 * s + gap;
    l->line_y = l->stop_y + 16 + 10 * sp;
    l->err_y = l->line_y + 4 * l->pitch + 8 * sp;
    l->foot_y = l->err_y + 3 * l->pitch + 12 * sp;
    l->rep_y = l->foot_y + 2 * l->pitch + 6 * sp;
    l->cx = sw / 2;
    l->text_w = sw - 16;
    l->icon_x = l->cx - 28 * s;
    l->bar_w = l->arc_r = 0;
    l->info_cx = l->cx;
    l->info_y = l->rep_y;
    l->info_w = l->text_w;
    if (!dump || compact) return;

    int bx = sw * 9 / 64;
    if (bx < 132) bx = 132;
    int edge = bx + 136;
    if (l->cx < edge + 180) l->cx = edge + 180;
    int a = l->cx - edge, b = sw - 8 - l->cx;
    l->text_w = 2 * (a < b ? a : b);
    if (l->text_w > 640) l->text_w = 640;
    l->icon_x = l->cx - 28 * s;
    l->bar_w = sh / 16;
    l->bar_h = sh * 9 / 20;
    l->border = s;
    l->bar_x = bx - l->bar_w / 2;
    l->bar_y = (sh - l->bar_h - 8 - 2 * l->pitch - 16) / 2;
    l->info_cx = l->bar_x + l->bar_w / 2;
    l->info_y = l->bar_y + l->bar_h + 8;
    l->info_w = 248;
    l->arc_x = l->icon_x - 2 * s;
    l->arc_y = l->icon_y + 20 * s;
    l->arc_r = (l->arc_x - l->bar_x - l->bar_w) * 3 / 4;
    int room = (l->arc_y - s) * 7 / 5;
    if (l->arc_r > room) l->arc_r = room;
}
