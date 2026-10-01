/* Shows PNG, JPEG, GIF and BMP pictures with zoom, scrolling and folder browsing. */
#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "shpath.h"
#include "fileopen.inc"
#include "wallpaper.inc"
#include "image.h"
#include "imgnav.inc"

static const Kapi *api;
static int my_type = -1;

#define WINW 480
#define WINH 360
#define ST_H 20
#define BG (C_G0 + 2)
#define MAXPIX (1024u * 1024u)
#define MAXBUDGET (24u * 1024u * 1024u)

/* One allocation, swapped with a single store, so a draw never sees a half-replaced picture. */
typedef struct { int w, h, ow, oh, fmt; u32 size; u8 px[]; } Pic;
static Pic *volatile pic;
static char spec[128];
static u8 fit = 1, dither = 1, drag;
static int zoom = 100, sx, sy, dx0, dy0, sx0, sy0;
static int last_cw = WINW, last_ch = WINH;
static char msg[96];

enum { B_OPEN, B_PREV, B_NEXT, B_FIT, B_ONE, B_OUT, B_IN, B_WALL, NB };
static const char *const blabel[NB] = { "Open", "<", ">", "Fit", "1:1", "-", "+", "Wallpaper" };
static const u8 bw[NB] = { 48, 24, 24, 36, 36, 24, 24, 84 };

static UiRect btn(int i)
{
    int x = 4;
    for (int k = 0; k < i; k++) x += bw[k] + (k == B_OPEN || k == B_NEXT || k == B_IN ? 10 : 3);
    return ui_r(x, 2, bw[i], UI_BTNH);
}

typedef struct { int pw, ph, dw, dh, vb, hb; } Geo;

static Geo geo(const Pic *p, int cw, int ch)
{
    Geo g = { cw, ch - UI_HDR - ST_H, 0, 0, 0, 0 };
    if (g.pw < 1) g.pw = 1;
    if (g.ph < 1) g.ph = 1;
    if (!p) return g;
    nav_display(p->ow, p->oh, g.pw, g.ph, fit, zoom, &g.dw, &g.dh);
    if (!fit) {
        if (g.dw > g.pw) { g.hb = 1; g.ph -= SB_W; }
        if (g.dh > g.ph) {
            g.vb = 1; g.pw -= SB_W;
            if (!g.hb && g.dw > g.pw) { g.hb = 1; g.ph -= SB_W; }
        }
        if (g.pw < 1) g.pw = 1;
        if (g.ph < 1) g.ph = 1;
    }
    return g;
}

static void clamp_scroll(void)
{
    Geo g = geo(pic, last_cw, last_ch);
    sx = nav_clamp(sx, g.dw, g.pw);
    sy = nav_clamp(sy, g.dh, g.ph);
}

static int cols[2048];
static u8 rowbuf[2048];

static void paint(const Pic *p, int x0, int y0, const Geo *g, int ox_s, int oy_s)
{
    int ox = g->dw < g->pw ? (g->pw - g->dw) / 2 : 0, oy = g->dh < g->ph ? (g->ph - g->dh) / 2 : 0;
    int w = g->dw - ox_s, h = g->dh - oy_s;
    if (w > g->pw - ox) w = g->pw - ox;
    if (h > g->ph - oy) h = g->ph - oy;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (oy) api->fill_rect(x0, y0, g->pw, oy, BG);
    if (oy + h < g->ph) api->fill_rect(x0, y0 + oy + h, g->pw, g->ph - oy - h, BG);
    if (ox) api->fill_rect(x0, y0 + oy, ox, h, BG);
    if (ox + w < g->pw) api->fill_rect(x0 + ox + w, y0 + oy, g->pw - ox - w, h, BG);
    if (!w || !h) return;
    int kx, ky, kw, kh;
    api->clip_rect_get(&kx, &ky, &kw, &kh);
    int xa = kx - (x0 + ox), xb = kx + kw - (x0 + ox), ya = ky - (y0 + oy), yb = ky + kh - (y0 + oy);
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > w) xb = w;
    if (yb > h) yb = h;
    if (xa >= xb || ya >= yb) return;
    if (g->dw == p->w && g->dh == p->h) {
        api->blit(x0 + ox + xa, y0 + oy + ya, xb - xa, yb - ya,
                  p->px + (u32)(ya + oy_s) * (u32)p->w + (u32)(xa + ox_s), p->w);
        return;
    }
    for (int x = xa; x < xb; x++) cols[x] = (int)((u32)(x + ox_s) * (u32)p->w / (u32)g->dw);
    for (int y = ya; y < yb; y++) {
        const u8 *src = p->px + (u32)((u32)(y + oy_s) * (u32)p->h / (u32)g->dh) * (u32)p->w;
        for (int x = xa; x < xb; x++) rowbuf[x] = src[cols[x]];
        api->blit(x0 + ox + xa, y0 + oy + y, xb - xa, 1, rowbuf + xa, xb - xa);
    }
}

static const char *fmt_name(int f)
{
    return f == IMG_PNG ? "PNG" : f == IMG_JPEG ? "JPEG" : f == IMG_GIF ? "GIF" : "BMP";
}

static void iv_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w;
    last_cw = cw; last_ch = ch;
    const Pic *p = pic;
    ui_header(cx, cy, cw, "");
    for (int i = 0; i < NB; i++) {
        int on = p && ((i != B_PREV && i != B_NEXT && i != B_WALL) || spec[0]);
        if (i == B_OPEN) on = 1;
        ui_button(cx, cy, btn(i), blabel[i], (i == B_FIT && fit && p) || (i == B_ONE && !fit && zoom == 100), on);
    }
    Geo g = geo(p, cw, ch);
    int top = cy + UI_HDR;
    if (!p) {
        api->fill_rect(cx, top, g.pw, g.ph, BG);
        api->draw_text_clip(cx + 12, top + 12, "Click Open, or open a picture from Files.", C_WHITE, g.pw - 24);
    } else {
        paint(p, cx, top, &g, nav_clamp(sx, g.dw, g.pw), nav_clamp(sy, g.dh, g.ph));
        if (g.vb) api->draw_sbar(cx + g.pw, top, g.ph, 0, g.dh, g.ph, sy);
        if (g.hb) api->draw_sbar(cx, top + g.ph, g.pw, 1, g.dw, g.pw, sx);
        if (g.vb && g.hb) api->fill_rect(cx + g.pw, top + g.ph, SB_W, SB_W, C_FACE);
    }
    char t[160];
    if (msg[0] || !p) api->strlcpy(t, msg, sizeof t);
    else api->kfmt(t, sizeof t, "%s   %s %dx%d   %u KB   %d%%", api->path_base(spec), fmt_name(p->fmt),
                   p->ow, p->oh, (p->size + 1023) / 1024, fit ? nav_fit_zoom(p->ow, g.dw) : zoom);
    ui_status(cx, cy, cw, ch, t);
}

static int iv_progress(void *ctx, int frac)
{
    (void)ctx;
    api->busy_set("Image Viewer", "Opening picture", frac);
    return api->esc_pending();
}

static u32 budget(void)
{
    u32 b = api->heap_avail() / 2;
    return b > MAXBUDGET ? MAXBUDGET : b;
}

/* Keeps the current picture when the new one can't be shown. */
static void show(const ImageOps *io, const u8 *data, u32 n, const char *s)
{
    ImgInfo in;
    int r = io->probe(data, n, &in);
    if (r) { api->strlcpy(msg, io->error(r), sizeof msg); return; }
    int tw = in.w, th = in.h;
    if ((u32)tw * (u32)th > MAXPIX) img_fit(in.w, in.h, 1024, 1024, &tw, &th);
    Pic *np = api->kmalloc(sizeof *np + (u32)tw * (u32)th);
    if (!np) { api->strlcpy(msg, io->error(IMG_ENOMEM), sizeof msg); return; }
    ImgReq q;
    api->memset(&q, 0, sizeof q);
    q.dst = np->px; q.dw = tw; q.dh = th; q.pitch = tw;
    q.flags = dither ? IMG_DITHER : 0; q.bg = BG;
    q.budget = budget(); q.progress = iv_progress;
    r = io->decode(data, n, &q);
    api->busy_end();
    if (r) { api->kfree(np); api->strlcpy(msg, io->error(r), sizeof msg); return; }
    np->w = tw; np->h = th; np->ow = in.w; np->oh = in.h; np->fmt = in.format; np->size = n;
    api->mem_track("Image Viewer picture", np, sizeof *np + (u32)tw * (u32)th);
    Pic *old = pic;
    pic = np;
    if (old) api->kfree(old);
    api->strlcpy(spec, s, sizeof spec);
    fit = 1; zoom = 100; sx = sy = 0; drag = 0; msg[0] = 0;
}

static void load(const char *s)
{
    const ImageOps *io = img_bind(api);
    if (!io) { api->strlcpy(msg, "Could not load image support (sys/image.kx)", sizeof msg); return; }
    int drive;
    char path[128];
    sh_spec_split(s, &drive, path, sizeof path);
    FileData fd;
    int r = fo_load(api, drive, path, 1, &fd, api->esc_pending);
    if (r) { api->strlcpy(msg, fo_error(r), sizeof msg); return; }
    show(io, fd.data, fd.size, s);
    fo_release(api, &fd);
}

static void picked(const char *path, void *ctx)
{
    (void)ctx;
    if (path && path[0]) load(path);
    api->win_redraw(my_type, 0);
}

static void step(int dir)
{
    if (!pic || !spec[0]) return;
    int drive;
    char path[128], dirp[128], name[FS_NAMELEN], joined[160], next[128];
    sh_spec_split(spec, &drive, path, sizeof path);
    api->path_dir(path, dirp, sizeof dirp);
    const char *cur = api->path_base(path), *best = 0, *wrap = 0;
    name[0] = 0;
    if (!drive) {
        char d[FS_NAMELEN];
        for (int i = 0; i < FS_NFILES; i++) {
            const FsEnt *e = api->fs_slot(i);
            if (!e || !e->used || (e->attr & FS_ATTR_DIR)) continue;
            api->path_dir(e->name, d, sizeof d);
            const char *b = api->path_base(e->name);
            if (!api->strcmp(d, dirp) && nav_is_picture(b)) nav_offer(cur, b, dir, &best, &wrap);
        }
        if (best || wrap) api->strlcpy(name, best ? best : wrap, sizeof name);
    } else {
        FatEnt *list = api->kmalloc(256 * sizeof *list);
        if (!list) { api->strlcpy(msg, "Not enough memory to list the folder", sizeof msg); return; }
        int n = api->fat_list(dirp[0] ? dirp : "/", list, 256);
        for (int i = 0; i < n; i++)
            if (!list[i].is_dir && nav_is_picture(list[i].name)) nav_offer(cur, list[i].name, dir, &best, &wrap);
        if (best || wrap) api->strlcpy(name, best ? best : wrap, sizeof name);
        api->kfree(list);
    }
    if (!name[0]) { api->strlcpy(msg, "No other pictures in this folder", sizeof msg); return; }
    api->path_join(joined, sizeof joined, drive && !dirp[0] ? "/" : dirp, name);
    sh_spec_make(drive, joined, next, sizeof next);
    load(next);
}

static void zoom_to(int z)
{
    const Pic *p = pic;
    if (!p) return;
    Geo a = geo(p, last_cw, last_ch);
    int mx = a.dw < a.pw ? a.dw / 2 : sx + a.pw / 2, my = a.dh < a.ph ? a.dh / 2 : sy + a.ph / 2;
    fit = 0; zoom = z;
    Geo b = geo(p, last_cw, last_ch);
    sx = (int)((u32)mx * (u32)b.dw / (u32)a.dw) - b.pw / 2;
    sy = (int)((u32)my * (u32)b.dh / (u32)a.dh) - b.ph / 2;
    clamp_scroll();
}

static void zoom_by(int dir)
{
    const Pic *p = pic;
    if (!p) return;
    int cur = zoom;
    if (fit) cur = nav_fit_zoom(p->ow, geo(p, last_cw, last_ch).dw);
    zoom_to(nav_zoom_step(cur, dir));
}

static void set_wallpaper(void)
{
    int drive;
    char path[128];
    sh_spec_split(spec, &drive, path, sizeof path);
    const char *p = path[0] == '/' ? path + 1 : path;
    if (drive) { api->strlcpy(msg, "Copy the picture to A: first to use it as wallpaper", sizeof msg); return; }
    if (api->strlen(p) >= sizeof api->cfg->wp_path) { api->strlcpy(msg, "The path is too long for a wallpaper", sizeof msg); return; }
    api->cfg->wp_mode = WP_BITMAP;
    api->strlcpy(api->cfg->wp_path, p, sizeof api->cfg->wp_path);
    if (!api->config_save()) { api->strlcpy(msg, "Could not save the wallpaper setting", sizeof msg); return; }
    api->broadcast("wallpaper", "");
    api->strlcpy(msg, "Wallpaper set", sizeof msg);
}

static void press(int i)
{
    msg[0] = 0;
    switch (i) {
    case B_OPEN: api->file_picker("Open picture", "", 0, picked, 0); break;
    case B_PREV: step(-1); break;
    case B_NEXT: step(1); break;
    case B_FIT:  fit = 1; sx = sy = 0; break;
    case B_ONE:  zoom_to(100); break;
    case B_OUT:  zoom_by(-1); break;
    case B_IN:   zoom_by(1); break;
    case B_WALL: if (pic) set_wallpaper(); break;
    }
}

static void iv_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst;
    last_cw = cw; last_ch = ch;
    ui_pointer(lx, ly, ev);
    for (int i = 0; i < NB; i++)
        if (ui_click(btn(i), lx, ly, ev)) { press(i); return; }
    const Pic *p = pic;
    if (!p) return;
    Geo g = geo(p, cw, ch);
    int y = ly - UI_HDR;
    if (ev == EV_PRESS) {
        drag = 0;
        if (g.vb && lx >= g.pw && lx < g.pw + SB_W && y >= 0 && y < g.ph) drag = 2;
        else if (g.hb && y >= g.ph && y < g.ph + SB_W && lx < g.pw) drag = 3;
        else if (!fit && lx < g.pw && y >= 0 && y < g.ph) { drag = 1; dx0 = lx; dy0 = ly; sx0 = sx; sy0 = sy; }
    }
    if (ev == EV_PRESS || ev == EV_DRAG) {
        if (drag == 2) sy = api->sbar_from_pos(g.ph, g.dh, g.ph, y);
        else if (drag == 3) sx = api->sbar_from_pos(g.pw, g.dw, g.pw, lx);
        else if (drag == 1) { sx = sx0 - (lx - dx0); sy = sy0 - (ly - dy0); }
        sx = nav_clamp(sx, g.dw, g.pw);
        sy = nav_clamp(sy, g.dh, g.ph);
    }
    if (ev == EV_RELEASE) drag = 0;
}

static void iv_wheel(int inst, int dz)
{
    (void)inst;
    if (!pic || fit) return;
    sy -= dz * 48;
    clamp_scroll();
    api->win_redraw(my_type, 0);
}

static void iv_key(int inst, int k)
{
    (void)inst;
    const Pic *p = pic;
    Geo g = geo(p, last_cw, last_ch);
    int pan_x = p && g.hb, pan_y = p && g.vb;
    msg[0] = 0;
    switch (k) {
    case K_PGDN: case ' ': step(1); break;
    case K_PGUP: case '\b': step(-1); break;
    case K_RIGHT: if (pan_x) sx += 48; else step(1); break;
    case K_LEFT:  if (pan_x) sx -= 48; else step(-1); break;
    case K_DOWN:  if (pan_y) sy += 48; break;
    case K_UP:    if (pan_y) sy -= 48; break;
    case K_HOME:  sx = sy = 0; break;
    case K_END:   sy = g.dh; break;
    case '+': case '=': zoom_by(1); break;
    case '-': case '_': zoom_by(-1); break;
    case 'f': case 'F': fit = 1; sx = sy = 0; break;
    case '1': zoom_to(100); break;
    case 'd': case 'D':
        dither = !dither;
        if (spec[0]) { char s[128]; api->strlcpy(s, spec, sizeof s); load(s); }
        break;
    case 'o': case 'O': press(B_OPEN); break;
    }
    clamp_scroll();
}

static int iv_opener(const char *name, const char *fullpath, const u8 *data, int n)
{
    const ImageOps *io = img_bind(api);
    char s[128];
    sh_spec_make(fullpath ? 1 : 0, fullpath ? fullpath : name, s, sizeof s);
    if (!io) api->strlcpy(msg, "Could not load image support (sys/image.kx)", sizeof msg);
    else if (!data || n <= 0) api->strlcpy(msg, "The file is empty", sizeof msg);
    else show(io, data, (u32)n, s);
    return api->win_open(my_type) < 0 ? -1 : 0;
}

static void iv_close(int inst)
{
    (void)inst;
    Pic *old = pic;
    pic = 0;
    if (old) api->kfree(old);
    spec[0] = msg[0] = 0;
    fit = 1; zoom = 100; sx = sy = 0; drag = 0;
}

static void iv_size(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }
static void iv_min(int *w, int *h) { *w = 360; *h = 160; }

const KextHeader kext_header = { KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Image Viewer" };

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    ui_init(k, gdi_bind(k, 11));
    static const AppDesc d = { .live_draw = APP_INDEPENDENT | APP_POINTER_FREE | APP_NO_CARET,
        .title = "Image Viewer", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .category = APP_CAT_PROGRAMS, .draw = iv_draw, .key = iv_key, .mouse = iv_mouse,
        .wheel = iv_wheel, .close = iv_close, .client_size = iv_size, .min_client = iv_min,
    };
    my_type = k->register_app(&d);
    if (my_type < 0) return 1;
    k->register_opener("png", iv_opener);
    k->register_opener("jpg", iv_opener);
    k->register_opener("jpeg", iv_opener);
    k->register_opener("gif", iv_opener);
    return 0;
}
