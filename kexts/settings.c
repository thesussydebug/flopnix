#include "kapi.h"
#include "gdi.h"
#include "shpath.h"
#include "netprefs.inc"
#include "netconfig.inc"
#include "bmp.inc"
#include "wallpaper.inc"

#include "ui.inc"

static const Kapi *api;
static const GdiOps *gfx;

#define kfmt            api->kfmt
#define strlen          api->strlen
#define strcmp          api->strcmp
#define strncmp         api->strncmp
#define strcasecmp      api->strcasecmp
#define strlcpy         api->strlcpy
#define memcpy          api->memcpy
#define memmove         api->memmove
#define memset          api->memset
#define human_size      api->human_size
#define human_size_kb   api->human_size_kb
#define SW              (*api->screen_w)
#define SH              (*api->screen_h)
#define mx              (*api->mouse_x)
#define my              (*api->mouse_y)
#define gui_blink       (*api->gui_blink)
#define fill_rect       api->fill_rect
#define hline           api->hline
#define vline           api->vline
#define bevel           api->bevel
#define panel           api->panel
#define draw_char       api->draw_char
#define draw_text       api->draw_text
#define draw_text_clip  api->draw_text_clip
#define draw_text_clip2 api->draw_text_clip2
#define draw_sbar       api->draw_sbar
#define sbar_from_pos   api->sbar_from_pos
#define focus_rect      api->focus_rect
#define blit            api->blit
#define palette_rgb     api->palette_rgb
#define palette_nearest api->palette_nearest
#define CFG             (api->cfg)
#define config_save     api->config_save
#define reboot          api->reboot
#define net_up          api->net_up
#define net_dhcp        api->net_dhcp
#define net_parse_ip    api->net_parse_ip

static int tab, view_scroll, view_h = 396, view_w = 440;
static int   selfield;
static char  fld[NP_FIELDS][32];
static int   fldlen[NP_FIELDS];
static char  msg[96];
static NetPrefs netdraft;
static int netmode, netpage;
static int   loaded;
static int   btnfocus = -1;
#define NWIDGET 22
static const u8 sst_vals[4] = { 15, 30, 60, 120 };

#define CW 440
#define CH 396

#define GD_Y 82
#define GD_H 116
#define GM_Y 82
#define GM_H 88
#define GS_Y 188
#define GS_H 112
#define GN_Y 82
#define GN_H 198
#define ACT_Y 346

static void settings_client_size(int *w, int *h);
static int settings_type=-1;
static int settings_open;
static u32 preview_generation;

static void ip_to_str(u32 ip, char *out)
{
    kfmt(out, 16, "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
}

static void load_fields(void)
{
    np_load(CFG, &netdraft); netmode = CFG->net_mode;
    ip_to_str(netmode ? CFG->ip : api->net_get(NET_IP), fld[0]);
    ip_to_str(netmode ? CFG->mask : api->net_get(NET_MASK), fld[1]);
    ip_to_str(netmode ? CFG->gw : api->net_get(NET_GW), fld[2]);
    ip_to_str(netmode || netdraft.dns_manual ? netdraft.dns : api->net_get(NET_DNS), fld[3]);
    ip_to_str(netmode || netdraft.dns_manual ? netdraft.dns2 : api->net_get(NET_DNS2), fld[4]);
    strlcpy(fld[5], netdraft.hostname[0] ? netdraft.hostname : "flopnix", 32);
    kfmt(fld[6], 32, "%x", netdraft.isa_io);
    kfmt(fld[7], 32, "%u", netdraft.mtu);
    for (int i = 0; i < NP_FIELDS; i++) fldlen[i] = strlen(fld[i]);
    selfield = -1; loaded = 1;
}

static void settings_init(void) { loaded = 0; tab = view_scroll = 0; }

typedef struct { int x, y, w, h; } Rect;

static Rect res_btn(int i) { Rect r = { 22 + (i % 2) * 144, GD_Y + 18 + (i / 2) * 28, 138, 24 }; return r; }

static Rect ms_btn(int i)  { Rect r = { 22 + (i % 2) * 134, GM_Y + 18 + (i / 2) * 28, 128, 24 }; return r; }

static Rect net_btn(int i) { Rect r = { 22 + i * 134, GN_Y + 18, 128, 24 }; return r; }

static Rect fld_box(int i) { Rect r = { 134, GN_Y + 80 + i * 28, 150, 22 }; return r; }

static Rect ss_btn(int i)  { Rect r = { 22 + i * 68, GS_Y + 18, 62, 24 }; return r; }
static Rect sst_btn(int i) { Rect r = { 22 + i * 64, GS_Y + 72, 60, 24 }; return r; }

static Rect act_btn(int i) {
    Rect r = {i == 0 ? 22 : i == 1 ? 12 : 146,
              i == 0 ? GN_Y + 164 : view_h - 52, i == 0 ? 164 : 128, 24}; return r;
}
static int compact_video(void) { return api->mem_total_kb() < 9216; }
static int widget_visible(int id) {
    if ((id == 1 || id == 2) && compact_video()) return 0;
    if (id == 14) return 1;
    if (tab == 2) return 0;
    if (id == 15) return tab == 0;
    if (id < 4) return tab == 0;
    if ((id >= 4 && id < 8) || id >= 16) return tab == 1;
    return tab == 2;
}

static Rect wp_open_btn(void) { Rect r = { 22, 226, 204, 26 }; return r; }

static int  page;
static u8   dm;
static u8   dcol[3];
static u8   dga[3], dgb[3];
static int  dgset;
static int  dtarget;
static char dpath[64];
static u8  *pv;
static char pv_of[64];
static char wmsg[44];
static int page_bottom(void){return tab==0?286:tab==1?300:netpage==0?328:netpage==1?338:348;}
static void settings_client_size(int *w,int *h)
{*w=CW;*h=page?(dm==WP_BITMAP?312:360):page_bottom()+34;}
static void fit_page(void)
{
    int w,h;settings_client_size(&w,&h);
    if(settings_type>=0)api->win_fit_client(settings_type,0,w,h);
    view_h=h+(page?0:24);view_w=w;
}

#define PVW 180
#define PVH 135
static Rect wp_mode_btn(int i) { Rect r = { 12 + i * ((view_w-24)/3), 32, (view_w-24)/3-3, 26 }; return r; }
static Rect wp_prev(void)      { Rect r = { (view_w - PVW) / 2, 64, PVW, PVH }; return r; }

static Rect wp_chip(int i)     { Rect r = { 112 + i * 96, 206, 88, 22 }; return r; }

#define WP_HUES  12
#define WP_SHADES 4
#define WP_NSW   (WP_HUES * WP_SHADES + WP_HUES)
static Rect wp_sw(int i)
{
    int col = i % WP_HUES, row = i / WP_HUES;

    Rect r = { 64 + col * 26, 234 + row * 18 + (row == WP_SHADES ? 4 : 0),
               24, 16 };
    return r;
}

static void wp_sw_rgb(int i, u8 *r, u8 *g, u8 *b)
{
    int col = i % WP_HUES, row = i / WP_HUES;
    if (row >= WP_SHADES) {
        u8 v = (u8)(col * 255 / (WP_HUES - 1));
        *r = *g = *b = v;
        return;
    }

    int h = col * 6 * 255 / WP_HUES;
    int seg = h / 255, f = h % 255;
    u8 up = (u8)f, dn = (u8)(255 - f);
    u8 cr = 0, cg = 0, cb = 0;
    switch (seg) {
    case 0: cr = 255; cg = up;  cb = 0;   break;
    case 1: cr = dn;  cg = 255; cb = 0;   break;
    case 2: cr = 0;   cg = 255; cb = up;  break;
    case 3: cr = 0;   cg = dn;  cb = 255; break;
    case 4: cr = up;  cg = 0;   cb = 255; break;
    default: cr = 255; cg = 0;  cb = dn;  break;
    }

    static const int mul[WP_SHADES] = { 100, 72, 46, 24 };
    static const int add[WP_SHADES] = { 90, 20, 0, 0 };
    *r = (u8)((cr * mul[row] / 100) + add[row] * (255 - cr) / 255);
    *g = (u8)((cg * mul[row] / 100) + add[row] * (255 - cg) / 255);
    *b = (u8)((cb * mul[row] / 100) + add[row] * (255 - cb) / 255);
}
static Rect wp_browse(void)    { Rect r = { 24, 206, 104, 22 }; return r; }
static Rect wp_act(int i) { Rect r = {12+i*((view_w-24)/3),view_h-32,(view_w-24)/3-6,24}; return r; }

static int hit(Rect r, int lx, int ly)
{
    return lx >= r.x && lx < r.x + r.w && ly >= r.y && ly < r.y + r.h;
}

static void wp_fill_rgb(int x, int y, int w, int h, const u8 *c)
{
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(x, y, w, h, GRGB(c[0], c[1], c[2]),
                           GRGB(c[0], c[1], c[2]), 1);
        gfx->set_dither(0);
    } else {
        fill_rect(x, y, w, h, palette_nearest(c[0], c[1], c[2]));
    }
}

static void pv_load_locked(void)
{
    if (!settings_open) return;
    u32 generation = preview_generation;
    if (pv && !strcmp(dpath, pv_of)) return;
    if (pv) { api->kfree(pv); pv = 0; }
    pv_of[0] = 0;
    if (!dpath[0]) return;

    int n = api->fs_read(dpath, api->iobuf, api->iobuf_size);
    if (!settings_open || generation != preview_generation) return;
    if (n <= 0) { strlcpy(wmsg, "cannot read that file", sizeof wmsg); return; }

    BmpHead h;
    const u8 *bm = api->iobuf;
    if (bmp_head(bm, (u32)n, &h) != 0) {
        strlcpy(wmsg, "not an 8 or 24-bit BMP", sizeof wmsg);
        return;
    }
    u8 map[256];
    if (h.bpp == 8)
        for (int i = 0; i < 256; i++) {
            u32 e = h.paloff + (u32)i * 4;
            map[i] = (e + 4 <= (u32)n)
                     ? palette_nearest(bm[e + 2], bm[e + 1], bm[e]) : C_BLACK;
        }
    u8 *b = api->kmalloc(PVW * PVH);
    if (!b) { strlcpy(wmsg, "out of memory", sizeof wmsg); return; }
    if(api->mem_track)api->mem_track("Wallpaper preview",b,PVW*PVH);
    memset(b, C_G0 + 2, PVW * PVH);
    for (int y = 0; y < PVH; y++) {
        int sy = bmp_src_row(&h, bmp_scale(y, PVH, h.h));
        if (!bmp_row_ok(&h, sy, (u32)n)) continue;
        const u8 *row = bm + h.off + (u32)sy * h.rowsz;
        for (int x = 0; x < PVW; x++) {
            int sx = bmp_scale(x, PVW, h.w);
            b[y * PVW + x] = h.bpp == 8 ? map[row[sx]]
                           : palette_nearest(row[sx * 3 + 2], row[sx * 3 + 1],
                                             row[sx * 3]);
        }
    }
    pv = b;
    strlcpy(pv_of, dpath, sizeof pv_of);
    kfmt(wmsg, sizeof wmsg, "%dx%d, %u-bit", h.w, h.h, h.bpp);
}
static void pv_load(void){api->buffer_lock();pv_load_locked();api->buffer_unlock();}


static void wp_picked(const char *path, void *ctx)
{
    (void)ctx;
    if (!settings_open || !path || !path[0]) return;
    int drive;
    char p[96];
    sh_spec_split(path, &drive, p, sizeof p);
    if (drive) {
        strlcpy(wmsg, "Copy the wallpaper to A: first.", sizeof wmsg);
        api->gui_dirty();
        return;
    }
    strlcpy(dpath, p, sizeof dpath);
    dm = WP_BITMAP;
    wmsg[0] = 0;
    pv_load();
    api->gui_dirty();
}

static const u8 WP_DEF_A[3] = { 36, 80, 100 };
static const u8 WP_DEF_B[3] = { 16, 34, 56 };

static void rgb_cpy(u8 *d, const u8 *s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }

static void wp_enter(void)
{
    page = 1;
    dm   = wp_mode_norm(CFG->wp_mode, CFG->wp_path);
    rgb_cpy(dcol, CFG->wp_col);
    dgset = wp_rgb_any(CFG->wp_ga, 3) || wp_rgb_any(CFG->wp_gb, 3);
    if (dgset) {
        rgb_cpy(dga, CFG->wp_ga);
        rgb_cpy(dgb, CFG->wp_gb);
    } else {
        rgb_cpy(dga, WP_DEF_A);
        rgb_cpy(dgb, WP_DEF_B);
    }
    dtarget = 0;
    strlcpy(dpath, CFG->wp_path, sizeof dpath);
    wp_path_fix(dpath, sizeof dpath);
    wmsg[0] = 0;
    if (dm == WP_BITMAP) pv_load();
}

static void wp_apply(void)
{
    CFG->wp_mode = dm;
    rgb_cpy(CFG->wp_col, dcol);

    if (dgset) { rgb_cpy(CFG->wp_ga, dga); rgb_cpy(CFG->wp_gb, dgb); }
    else       { u8 z[3] = {0,0,0}; rgb_cpy(CFG->wp_ga, z); rgb_cpy(CFG->wp_gb, z); }
    strlcpy(CFG->wp_path, dm == WP_BITMAP ? dpath : "", sizeof CFG->wp_path);
    if (config_save()) {
        api->broadcast("wallpaper", "");
        strlcpy(wmsg, "wallpaper applied", sizeof wmsg);
    } else {
        strlcpy(wmsg, "could not write the config sector", sizeof wmsg);
    }
}

static void button(int cx, int cy, Rect r, const char *label, int on);
#include "net_settings.inc"

static Rect widget_rect(int id)
{
    if (id < 4) return res_btn(id);
    if (id < 8) return ms_btn(id - 4);
    if (id < 10) return net_btn(id - 8);
    if (id < 13) return fld_box(id - 10);
    if (id < 16) return act_btn(id - 13);
    if (id < 18) return ss_btn(id - 16);
    return sst_btn(id - 18);
}

static void widget_activate(int id)
{
    if ((id == 1 || id == 2) && compact_video()) return;
    if (id < 4) { CFG->video = id + 1; strlcpy(msg, "Save, then restart to change screen size", sizeof msg); return; }
    if (id < 8) { CFG->mouse_speed = id - 3; return; }
    if (id < 10) { CFG->net_mode = id - 8; selfield = -1; load_fields(); return; }
    if (id < 13) { if (CFG->net_mode == 1) selfield = id - 10; return; }
    if (id == 13) { apply_network(); return; }
    if (id == 14) { if (tab == 2 && !apply_network()) return; strlcpy(msg, config_save() ? (tab == 2 && net_restart_pending ? "Saved. Restart to use the new adapter settings." : "Your settings have been saved") : "Could not save to the floppy", sizeof msg); return; }
    if (id == 15) { if (config_save()) reboot(); else strlcpy(msg, "Could not save; restart cancelled", sizeof msg); return; }
    if (id == 16) { CFG->ss_enable = 0; return; }
    if (id == 17) { CFG->ss_enable = 1; return; }
    if (id >= 18 && id < 22) { CFG->ss_secs = sst_vals[id - 18]; return; }
}

static void settings_key(int k)
{
    if (!loaded) load_fields();
    if (page) { if (k == 27) page = 0; return; }
    if (tab == 2) { network_key(k); return; }
    if (selfield >= 0) {
        int i = selfield;
        if (k == 27 || k == '\n') { selfield = -1; return; }
        if (k == '\b') { if (fldlen[i]) fld[i][--fldlen[i]] = 0; return; }
        if (((k >= '0' && k <= '9') || k == '.') && fldlen[i] < 15) {
            fld[i][fldlen[i]++] = (char)k;
            fld[i][fldlen[i]] = 0;
        }
        return;
    }
    if (k == '\t') {
        do {
            btnfocus++;
            if (btnfocus >= NWIDGET) { btnfocus = -1; break; }
        } while (!widget_visible(btnfocus) || (btnfocus >= 10 && btnfocus < 13 && CFG->net_mode != 1));
        return;
    }
    if (k == 27) { btnfocus = -1; return; }
    if (k == '\n' && btnfocus >= 0) { msg[0] = 0; widget_activate(btnfocus); return; }
}

static void wp_mouse(int lx, int ly)
{
    for (int i = 0; i < 3; i++)
        if (hit(wp_mode_btn(i), lx, ly)) {
            dm = (u8)i;
            wmsg[0] = 0;
            if (dm == WP_BITMAP) pv_load();
            return;
        }
    if (dm == WP_BITMAP) {
        if (hit(wp_browse(), lx, ly)) {
            api->file_picker("Choose a wallpaper", "bmp", 0, wp_picked, 0);
            return;
        }
    } else {
        if (dm == WP_GRADIENT)
            for (int i = 0; i < 2; i++)
                if (hit(wp_chip(i), lx, ly)) { dtarget = i; return; }
        for (int i = 0; i < WP_NSW; i++)
            if (hit(wp_sw(i), lx, ly)) {
                u8 c[3];
                wp_sw_rgb(i, &c[0], &c[1], &c[2]);
                if (dm == WP_SOLID)     rgb_cpy(dcol, c);
                else if (dtarget == 0) { rgb_cpy(dga, c); dgset = 1; }
                else                   { rgb_cpy(dgb, c); dgset = 1; }
                return;
            }
    }
    if (hit(wp_act(0), lx, ly)) { wp_apply(); return; }
    if (hit(wp_act(1), lx, ly)) { wp_enter(); return; }
    if (hit(wp_act(2), lx, ly)) { page = 0; return; }
}

static void settings_mouse(int lx, int ly, int ev)
{
    if (ev != EV_PRESS) return;
    if (!loaded) load_fields();
    msg[0] = 0;
    btnfocus = -1;

    if (page) { wp_mouse(lx, ly); return; }
    if (ly >= 32 && ly < 58) {
        int n = (lx - 12) / ((view_w - 24) / 3);
        if (lx >= 12 && n >= 0 && n < 3) { tab = n; view_scroll = 0; selfield = -1; }
        return;
    }
    if (ly >= view_h - 56) {
        for (int id = 14; id <= 15; id++)
            if (widget_visible(id) && hit(widget_rect(id), lx, ly)) { widget_activate(id); return; }
        return;
    }
    if (ly < 68) return;
    ly += view_scroll;
    if (tab == 2) { network_mouse(lx, ly); return; }
    if (tab == 0 && hit(wp_open_btn(), lx, ly)) { wp_enter(); return; }

    for (int id = 0; id < NWIDGET; id++)
        if (widget_visible(id) && id != 14 && id != 15 && hit(widget_rect(id), lx, ly)) { widget_activate(id); return; }
    selfield = -1;
}

static void button(int cx, int cy, Rect r, const char *label, int on)
{
    button_label(api,cx+r.x,cy+r.y,r.w,r.h,label,on,1);
}

static void wp_draw(int cx, int cy, int cw)
{
    if (gfx) { gfx->set_dither(1); gfx->fill_gradient(cx, cy, cw, 26, GRGB(214, 218, 228), GRGB(180, 186, 202), 1); gfx->set_dither(0); }
    draw_text(cx + 12, cy + 8, "Wallpaper", C_NAVY);

    if (wmsg[0]) draw_text_clip(cx + 100, cy + 8, wmsg, C_MAROON, cw - 112);

    const char *names[3] = { "Gradient", "Solid colour", "Bitmap" };
    for (int i = 0; i < 3; i++)
        button(cx, cy, wp_mode_btn(i), names[i], dm == i);

    Rect p = wp_prev();
    panel(cx + p.x - 2, cy + p.y - 2, p.w + 4, p.h + 4, 1);
    if (dm == WP_SOLID) {
        wp_fill_rgb(cx + p.x, cy + p.y, p.w, p.h, dcol);
    } else if (dm == WP_BITMAP) {
        if (pv) {
            blit(cx + p.x, cy + p.y, PVW, PVH, pv, PVW);
        } else {
            fill_rect(cx + p.x, cy + p.y, p.w, p.h, C_G0 + 2);
            draw_text(cx + p.x + 26, cy + p.y + p.h / 2 - 4,
                      "no image chosen", C_G0 + 5);
        }
    } else if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx + p.x, cy + p.y, p.w, p.h,
                           GRGB(dga[0], dga[1], dga[2]),
                           GRGB(dgb[0], dgb[1], dgb[2]), 1);
        gfx->set_dither(0);
    } else {
        fill_rect(cx + p.x, cy + p.y, p.w, p.h,
                  palette_nearest(dga[0], dga[1], dga[2]));
    }

    if (dm == WP_BITMAP) {
        button(cx, cy, wp_browse(), "Choose...", 0);
        Rect b = wp_browse();
        draw_text_clip(cx + b.x + b.w + 10, cy + b.y + 7,
                       dpath[0] ? dpath : "(no image chosen)", C_BLACK,
                       cw - b.x - b.w - 20);
        draw_text(cx + 12, cy + 244,
                  "8 or 24-bit BMP. Paint saves these.", C_G0 + 4);
        draw_text(cx + 12, cy + 260,
                  "The image is stretched to fill the screen.", C_G0 + 4);
    } else {

        if (dm == WP_GRADIENT) {
            draw_text(cx + 64, cy + 212, "Ends:", C_BLACK);
            const char *ends[2] = { "Top", "Bottom" };
            const u8 *col[2] = { dga, dgb };
            for (int i = 0; i < 2; i++) {
                Rect c = wp_chip(i);
                panel(cx + c.x, cy + c.y, c.w, c.h, dtarget == i);
                wp_fill_rgb(cx + c.x + 3, cy + c.y + 3, 20, c.h - 6, col[i]);
                draw_text(cx + c.x + 30, cy + c.y + 7, ends[i],
                          dtarget == i ? C_NAVY : C_BLACK);
            }
        } else {
            draw_text(cx + 12, cy + 212, "Colour:", C_BLACK);
            Rect c = wp_chip(0);
            panel(cx + c.x, cy + c.y, c.w, c.h, 1);
            wp_fill_rgb(cx + c.x + 3, cy + c.y + 3, c.w - 6, c.h - 6, dcol);
        }
        const u8 *cur = dm == WP_SOLID ? dcol : (dtarget ? dgb : dga);
        for (int i = 0; i < WP_NSW; i++) {
            Rect s = wp_sw(i);
            u8 c[3];
            wp_sw_rgb(i, &c[0], &c[1], &c[2]);
            wp_fill_rgb(cx + s.x, cy + s.y, s.w, s.h, c);

            if (c[0] == cur[0] && c[1] == cur[1] && c[2] == cur[2]) {
                bevel(cx + s.x - 2, cy + s.y - 2, s.w + 4, s.h + 4, 0);
                focus_rect(cx + s.x - 2, cy + s.y - 2, s.w + 4, s.h + 4);
            }
        }
    }

    button(cx, cy, wp_act(0), "Apply", 0);
    button(cx, cy, wp_act(1), "Revert", 0);
    button(cx, cy, wp_act(2), "Back", 0);
}

static void settings_draw(Win *w, int cx, int cy, int cw)
{
    (void)w;
    if (!loaded) load_fields();
    fill_rect(cx, cy, cw, view_h, C_FACE);
    if (page) { wp_draw(cx, cy, cw); return; }
    menu_shade(api,cx,cy+24,cw,38,0);

    const char *tabs[3] = {"Display", "Mouse & idle", "Network"};
    for (int i = 0; i < 3; i++) {
        Rect r = {12 + i * ((cw - 24) / 3), 32, (cw - 24) / 3 - 3, 26};
        button(cx, cy, r, tabs[i], tab == i);
    }
    int original_y = cy;
    int vh = view_h - 126;
    if (vh < 16) vh = 16;
    int max_scroll = page_bottom() - 68 - vh;
    if (max_scroll < 0) max_scroll = 0;
    if (view_scroll > max_scroll) view_scroll = max_scroll;
    api->set_clip(cx, cy + 68, cw, vh);
    cy -= view_scroll;
    if (tab == 0) {
        ui_group(cx + 12, cy + GD_Y, cw - 24, GD_H, "Screen size");
        const char *res[4] = {"640 x 480", "800 x 600", "1024 x 768", "Safe VGA"};
        int choice = CFG->video;
        if (compact_video() && (choice == 2 || choice == 3)) choice = 1;
        for (int i = 0; i < 4; i++) {
            Rect r = res_btn(i);
            ui_button(cx, cy, ui_r(r.x,r.y,r.w,r.h), res[i], choice == i + 1,
                      !compact_video() || i == 0 || i == 3);
        }
        draw_text_clip(cx + 22, cy + GD_Y + 82, compact_video() ? "Low memory: 640 x 480 or Safe VGA." : "Larger sizes fit more on screen.", C_G0 + 2, cw - 44);
        button(cx, cy, wp_open_btn(), "Change background...", 0);
        draw_text_clip(cx + 22, cy + 270, "Screen size changes after a restart.", C_G0 + 2, cw - 44);
    } else if (tab == 1) {
        ui_group(cx + 12, cy + GM_Y, cw - 24, GM_H, "Pointer speed");
        const char *speeds[4] = {"Slow", "Normal", "Fast", "Faster"};
        for (int i = 0; i < 4; i++) button(cx, cy, ms_btn(i), speeds[i], CFG->mouse_speed == i + 1);
        ui_group(cx + 12, cy + GS_Y, cw - 24, GS_H, "Screen saver");
        button(cx, cy, ss_btn(0), "Off", !CFG->ss_enable);
        button(cx, cy, ss_btn(1), "On", CFG->ss_enable);
        draw_text(cx + 22, cy + GS_Y + 48, "Start after no activity:", C_G0 + 2);
        const char *times[4] = {"15 sec", "30 sec", "1 min", "2 min"};
        for (int i = 0; i < 4; i++) button(cx, cy, sst_btn(i), times[i], CFG->ss_secs == sst_vals[i]);
    } else {
        network_draw(cx, cy, cw);
    }
    if (btnfocus >= 0 && widget_visible(btnfocus) && btnfocus != 14 && btnfocus != 15) {
        Rect r = widget_rect(btnfocus); focus_rect(cx + r.x + 1, cy + r.y + 1, r.w - 2, r.h - 2);
    }
    cy = original_y;
    api->set_clip(cx, cy, cw, view_h);
    button(cx, cy, act_btn(1), "Save changes", 0);
    if (tab == 0) button(cx, cy, act_btn(2), "Save & restart", 0);
    ui_status(cx, cy, cw, view_h, msg[0] ? msg : max_scroll ? "Scroll for more options. Save to keep changes." : "Save changes to keep them after restarting.");
}

static void set_csize(int inst, int *w, int *h) { (void)inst; settings_client_size(w, h); }
static void set_open(int inst)
{
    (void)inst;
    settings_open = 1;
    preview_generation++;
}
static void set_close(int inst)
{
    (void)inst;
    settings_open = 0;
    preview_generation++;
    if (pv) { api->kfree(pv); pv = 0; }
    pv_of[0] = 0;
    page = view_scroll = 0;
}
static void set_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    view_h = ch + (page ? 0 : 24); view_w = cw; settings_draw(w, cx, cy - (page ? 0 : 24), cw); }
static void set_wheel(int inst, int dz) { (void)inst; if (!page) { view_scroll -= dz * 24; if (view_scroll < 0) view_scroll = 0; api->gui_dirty(); } }
static void set_key(int inst, int k) { (void)inst; settings_key(k);fit_page(); }
static void set_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{ (void)inst; (void)cw; (void)ch; settings_mouse(lx, ly + (page ? 0 : 24), ev);fit_page(); }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, KEXT_RECLAIMABLE, "Settings"
};

static void confsec(const char *args)
{
    union {FCfg cfg;u8 bytes[512];} snapshot;u8 *sector=snapshot.bytes;char line[160];
    while(*args==' ')args++;
    int raw=!strcmp(args,"raw");
    if(*args&&!raw){api->shell_print("Usage: confsec [raw]\n");return;}
    if(api->config_read(sector,sizeof snapshot)!=512){api->shell_print("Settings sector is unavailable.\n");return;}
    api->shell_print("Settings sector (LBA 256) - 512 bytes - RAM snapshot\n");
    if(!raw){
        const FCfg *c=&snapshot.cfg;char path[65];memcpy(path,c->wp_path,64);path[64]=0;
        kfmt(line,sizeof line,"04  Display mode: %u     06  Mouse speed: %u\n",c->video,c->mouse_speed);api->shell_print(line);
        kfmt(line,sizeof line,"05  Network: %s\n",c->net_mode?"Static IPv4":"Automatic (DHCP)");api->shell_print(line);
        const char *names[]={"Static IP","Netmask","Gateway"};
        for(int i=0;i<3;i++){const u8 *p=sector+8+4*i;kfmt(line,sizeof line,"%02x  %-10s %u.%u.%u.%u\n",8+4*i,names[i],p[0],p[1],p[2],p[3]);api->shell_print(line);}
        kfmt(line,sizeof line,"07  Screen saver: %s   14  Delay: %u seconds\n",c->ss_enable?"On":"Off",c->ss_secs);api->shell_print(line);
        kfmt(line,sizeof line,"15  Wallpaper mode: %u\n17  Image: %s\n",c->wp_mode,path);api->shell_print(line);
        kfmt(line,sizeof line,"57  Color RGB: %u, %u, %u\n",c->wp_col[0],c->wp_col[1],c->wp_col[2]);api->shell_print(line);
        kfmt(line,sizeof line,"5a  Gradient RGB: %u,%u,%u to %u,%u,%u\n",c->wp_ga[0],c->wp_ga[1],c->wp_ga[2],c->wp_gb[0],c->wp_gb[1],c->wp_gb[2]);api->shell_print(line);
        kfmt(line,sizeof line,"60  Time zone: %d quarter-hours from UTC\n",c->tz_qh);api->shell_print(line);
        u32 automatic=0;api->config_get("remote.auto",&automatic);
        kfmt(line,sizeof line,"Automatic Remote: %s at boot (saved by the Debug checkbox)\n",automatic==1?"On":"Off");api->shell_print(line);
        api->shell_print("Offsets are hexadecimal. Remaining bytes hold network\nand app preferences; unused bytes normally stay zero.\n");
        api->shell_print("Use Save changes in Settings to write to disk.\nWallpaper uses Apply; network fields must be applied.\nRun confsec again to refresh; confsec raw shows all bytes.\n");return;
    }
    for(int row=0;row<(int)sizeof snapshot;row+=16){
        kfmt(line,sizeof line,"%03x ",row);int at=4;
        for(int i=0;i<16;i++){kfmt(line+at,sizeof line-at,"%02x ",sector[row+i]);at+=3;}
        line[at++]=' ';for(int i=0;i<16;i++){u8 c=sector[row+i];line[at++]=c>=32&&c<127?c:'.';}
        line[at++]='\n';line[at]=0;api->shell_print(line);
    }
    api->shell_print("512 bytes printed (hex 000-1ff). PgUp or wheel to scroll up.\n");
}

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    ui_init(k, gfx);
    settings_init();
    k->register_cmd("confsec","confsec [raw] - inspect current settings",confsec);
    static const AppDesc d = {.live_draw=APP_INDEPENDENT,
        .title = "Settings", .max_inst = 1, .in_menu = 1,
        .draw = set_draw, .key = set_key, .mouse = set_mouse, .wheel = set_wheel,
        .client_size = set_csize, .open = set_open, .close = set_close,
    };
    settings_type=api->register_app(&d);return settings_type<0;
}
