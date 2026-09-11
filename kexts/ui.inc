/* Draws shared app buttons, menus, and panels. */
#define UI_HDR   26
#define UI_BTNH  22
#define UI_ROWH  14
#define UI_PAD   12
#define UI_GTOP  12

typedef struct { int x, y, w, h; } UiRect;

static const Kapi   *ui_api;
static const GdiOps *ui_gfx;

static inline void ui_init(const Kapi *k, const GdiOps *g)
{
    ui_api = k;
    ui_gfx = g;
}

static inline UiRect ui_r(int x, int y, int w, int h)
{
    UiRect r = { x, y, w, h };
    return r;
}

static inline int ui_hit(UiRect r, int lx, int ly)
{
    return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
}

static inline void ui_header(int cx, int cy, int cw, const char *title)
{
    ui_gfx = gdi_bind(ui_api, 11);
    if (ui_gfx) {
        ui_gfx->set_dither(1);
        ui_gfx->fill_gradient(cx, cy, cw, UI_HDR, GRGB(214, 218, 228),
                              GRGB(180, 186, 202), 1);
        ui_gfx->set_dither(0);
    } else ui_api->fill_rect(cx, cy, cw, UI_HDR, C_FACE);
    ui_api->hline(cx, cy + UI_HDR - 1, cw, C_SHAD);
    ui_api->draw_text(cx + 10, cy + 8, title, C_NAVY);
}

static inline void ui_header_right(int cx, int cy, int cw, const char *s, u8 col)
{
    ui_api->draw_text(cx + cw - 10 - ui_api->text_width(s), cy + 8, s, col);
}

static inline void ui_group(int x, int y, int w, int h, const char *title)
{
    int tw = title && title[0] ? ui_api->text_width(title) : 0;
    int gap = tw ? tw + 8 : 0;

    ui_api->hline(x, y, 6, C_SHAD);
    ui_api->hline(x + 6 + gap, y, w - 6 - gap, C_SHAD);
    ui_api->hline(x, y + 1, 6, C_LIGHT);
    ui_api->hline(x + 6 + gap, y + 1, w - 6 - gap, C_LIGHT);

    ui_api->vline(x, y, h, C_SHAD);
    ui_api->vline(x + 1, y + 1, h - 1, C_LIGHT);
    ui_api->vline(x + w - 2, y, h, C_SHAD);
    ui_api->vline(x + w - 1, y, h, C_LIGHT);
    ui_api->hline(x, y + h - 2, w, C_SHAD);
    ui_api->hline(x, y + h - 1, w, C_LIGHT);
    if (tw) ui_api->draw_text(x + 10, y - 4, title, C_NAVY);
}

static inline int ui_gx(int x) { return x + 10; }
static inline int ui_gy(int y) { return y + UI_GTOP; }

static inline void ui_button(int cx, int cy, UiRect r, const char *label,
                      int pressed, int enabled)
{
    ui_api->panel(cx + r.x, cy + r.y, r.w, r.h, pressed);
    int tw = ui_api->text_width(label);
    int tx = cx + r.x + (r.w - tw) / 2 + (pressed ? 1 : 0);

    int ty = cy + r.y + (r.h - 16) / 2 + 1 + (pressed ? 1 : 0);

    if (!enabled) {
        ui_api->draw_text(tx + 1, ty + 1, label, C_LIGHT);
        ui_api->draw_text(tx, ty, label, C_G0 + 3);
    } else ui_api->draw_text(tx, ty, label, C_BLACK);
}

static inline void ui_row(int x, int y, const char *label, const char *value, int colw)
{
    ui_api->draw_text(x, y, label, C_G0 + 3);
    ui_api->draw_text(x + colw, y, value, C_BLACK);
}

static inline void ui_progress(int x, int y, int w, int h, int pct, int show_pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ui_api->panel(x, y, w, h, 1);
    int fw = (w - 2) * pct / 100;
    if (fw > 0) ui_api->fill_rect(x + 1, y + 1, fw, h - 2, C_BBLUE);
    if (show_pct) {
        char t[8];
        ui_api->kfmt(t, sizeof t, "%d%%", pct);
        int tw = ui_api->text_width(t);
        int tx = x + (w - tw) / 2, ty = y + (h - 16) / 2 + 1;

        ui_api->draw_text_clip2(tx, ty, t, C_WHITE, x + 1, x + 1 + fw);
        ui_api->draw_text_clip2(tx, ty, t, C_BLACK, x + 1 + fw, x + w - 1);
    }
}

static inline int ui_swatch(int x, int y, u8 col, const char *label)
{

    ui_api->fill_rect(x, y + 3, 8, 8, col);
    ui_api->bevel(x - 1, y + 2, 10, 10, 1);
    ui_api->draw_text(x + 13, y, label, C_BLACK);
    return 13 + ui_api->text_width(label) + 14;
}

static inline void ui_status(int cx, int cy, int cw, int ch, const char *text)
{
    int y = cy + ch - 20;
    ui_api->fill_rect(cx, y, cw, 20, C_FACE);
    ui_api->hline(cx, y, cw, C_SHAD);
    ui_api->hline(cx, y + 1, cw, C_LIGHT);
    if (text && text[0])
        ui_api->draw_text_clip(cx + 8, y + 3, text, C_BLACK, cw - 16);
}
