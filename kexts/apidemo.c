/* Demonstrates kernel API calls in a small app. */
#include "kapi.h"

static const Kapi *api;

static char v_uuid[40], v_b64[24], v_crc[20], v_date[32], v_path[48];
static char status[64];

static void refresh_values(void)
{
    api->uuid_gen(v_uuid);
    api->b64_encode((const u8 *)"FLOPNIX", 7, v_b64, sizeof v_b64);
    api->kfmt(v_crc, sizeof v_crc, "%08x", api->crc32("FLOPNIX", 7));
    api->date_fmt(api->rtc_now_dos(), "%a %d %b %Y  %H:%M", v_date, sizeof v_date);
    const char *p = "u:/photos/trip.bmp";
    api->kfmt(v_path, sizeof v_path, "base=%s ext=%s", api->path_base(p), api->path_ext(p));
}

static void on_msg(int result, void *ctx)
{
    (void)ctx;
    const char *r = result == MBR_YES ? "You chose Yes"
                  : result == MBR_NO ? "You chose No" : "Cancelled";
    api->notify(r);
}
static void on_pick(const char *path, void *ctx)
{
    (void)ctx;
    if (path) api->kfmt(status, sizeof status, "picked: %s", path);
    else api->strlcpy(status, "picker cancelled", sizeof status);
    api->gui_dirty();
}

static const char *const btns[] = { "Message", "Notify", "Pick file", "Progress" };
#define NB 4
#define BTN_Y0 116

static void demo_open(int inst) { (void)inst; refresh_values(); status[0] = 0; }

static void demo_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w; (void)ch;
    api->draw_text(cx + 10, cy + 8, "KAPI v7 services", C_NAVY);
    int y = cy + 30;
    api->draw_text(cx + 10, y, "uuid:", C_G0 + 2); api->draw_text(cx + 60, y, v_uuid, C_BLACK); y += 16;
    api->draw_text(cx + 10, y, "b64:",  C_G0 + 2); api->draw_text(cx + 60, y, v_b64, C_BLACK); y += 16;
    api->draw_text(cx + 10, y, "crc32:",C_G0 + 2); api->draw_text(cx + 60, y, v_crc, C_BLACK); y += 16;
    api->draw_text(cx + 10, y, "date:", C_G0 + 2); api->draw_text(cx + 60, y, v_date, C_BLACK); y += 16;
    api->draw_text(cx + 10, y, v_path, C_BLACK);

    for (int i = 0; i < NB; i++) {
        int bx = cx + 10 + (i % 2) * 150, by = cy + BTN_Y0 + (i / 2) * 28;
        int hov = *api->mouse_x >= bx && *api->mouse_x < bx + 140 &&
                  *api->mouse_y >= by && *api->mouse_y < by + 22;
        api->panel(bx, by, 140, 22, hov);
        api->draw_text(bx + (140 - (int)api->strlen(btns[i]) * 8) / 2, by + 4, btns[i], C_BLACK);
    }
    if (status[0])
        api->draw_text_clip(cx + 10, cy + BTN_Y0 + 60, status, C_MAROON, cw - 20);
}

static void demo_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    for (int i = 0; i < NB; i++) {
        int bx = 10 + (i % 2) * 150, by = BTN_Y0 + (i / 2) * 28;
        if (lx < bx || lx >= bx + 140 || ly < by || ly >= by + 22) continue;
        if (i == 0)
            api->msgbox("Confirm", "Delete the selected file? This cannot be undone.",
                        MB_YESNO, on_msg, 0);
        else if (i == 1)
            api->notify("Saved to A: successfully");
        else if (i == 2)
            api->file_picker("Open a file", "", 0, on_pick, 0);
        else {
            api->progress_open("Copying");
            for (int p = 0; p <= 100; p += 5) {
                char l[24];
                api->kfmt(l, sizeof l, "file %d of 20", p / 5);
                api->progress_set(p, l);
                api->sleep_ms(60);
            }
            api->progress_close();
            api->notify("Copy complete");
        }
        return;
    }
}

static void demo_csize(int inst, int *w, int *h) { (void)inst; *w = 320; *h = 226; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "API Demo"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    static const AppDesc d = {
        .title = "API Demo", .max_inst = 1, .in_menu = 1,
        .open = demo_open, .draw = demo_draw, .mouse = demo_mouse,
        .client_size = demo_csize,
    };
    return k->register_app(&d) < 0;
}
