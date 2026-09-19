#include "apptext.h"

typedef struct {
    char *buf;
    int cap, len, caret, anchor, selecting, offset;
} AppField;

static void af_set(AppField *t, char *buf, int cap, const char *value)
{
    api->strlcpy(buf, value, cap);
    t->buf = buf; t->cap = cap; t->len = (int)api->strlen(buf);
    t->caret = t->anchor = t->len; t->selecting = t->offset = 0;
}

static void af_key(AppField *t, int k)
{
    TextEdit text = { t->buf, t->cap, t->len, t->caret, t->anchor };
    if (at_key(&text, k, 0, 0, 0) < 0) api->notify("Text did not fit; original kept.");
    t->len = text.len; t->caret = text.caret; t->anchor = text.anchor;
}

static int af_offset(AppField *t, int width)
{
    int cols = (width - 12) / 8;
    if (cols < 1) cols = 1;
    if (t->caret < t->offset) t->offset = t->caret;
    if (t->caret >= t->offset + cols) t->offset = t->caret - cols + 1;
    if (t->offset < 0) t->offset = 0;
    return cols;
}

static void af_draw(AppField *t, int x, int y, int width, int focused)
{
    api->panel(x,y,width,24,1); api->fill_rect(x+2,y+2,width-4,20,C_WHITE);
    int cols = af_offset(t, width);
    TextEdit text = { t->buf, t->cap, t->len, t->caret, t->anchor };
    for (int c = 0; c < cols && t->offset + c < t->len; c++) {
        int pos = t->offset + c, selected = focused && te_selected(&text, pos);
        if (selected) api->fill_rect(x+6+c*8,y+4,8,16,C_NAVY);
        api->draw_char(x+6+c*8,y+4,t->buf[pos],selected?C_WHITE:C_BLACK);
    }
    if (focused && t->caret == t->anchor && *api->gui_blink)
        api->vline(x+6+(t->caret-t->offset)*8,y+4,15,C_BLACK);
}

static int af_mouse(AppField *t, int x, int y, int width, int mx, int my, int ev)
{
    if (ev == EV_RELEASE) { int was = t->selecting; t->selecting = 0; return was; }
    if (ev == EV_PRESS) {
        t->selecting = mx >= x && mx < x + width && my >= y && my < y + 24;
        if (!t->selecting) return 0;
    } else if (ev != EV_DRAG || !t->selecting) return 0;
    af_offset(t, width);
    int col = (mx - x - 6 + 4) / 8;
    if (mx < x + 6) col = -1;
    int pos = t->offset + col;
    if (pos < 0) pos = 0;
    if (pos > t->len) pos = t->len;
    t->caret = pos;
    if (ev == EV_PRESS && !(api->kbd_mods() & 1)) t->anchor = pos;
    af_offset(t, width);
    return 1;
}
