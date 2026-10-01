#include "kapi.h"
#include "gdi.h"

#define BANNER_H 68

static const Kapi *api;
static const GdiOps *g;

static void banner(int cx, int cy, int cw)
{
    char v[40];
    int fx = cx + 10, fy = cy + 14, tx = cx + 62;
    if (g) {
        g->set_dither(1);
        g->fill_gradient(cx, cy, cw, BANNER_H, GRGB(70, 120, 200), GRGB(18, 42, 96), 1);
        g->set_dither(0);
    } else {
        api->fill_rect(cx, cy, cw, BANNER_H, C_NAVY);
    }
    api->fill_rect(fx, fy, 40, 40, C_NAVY);
    api->fill_rect(fx + 10, fy, 20, 14, C_SILVER);
    api->fill_rect(fx + 20, fy + 2, 6, 10, C_NAVY);
    api->fill_rect(fx + 6, fy + 20, 28, 20, C_WHITE);
    api->kfmt(v, sizeof v, "FLOPNIX %s build %s", api->os_version, api->os_build_num);
    api->draw_text(tx, cy + 14, v, C_WHITE);
    api->draw_text(tx, cy + 32, "by tar0byte", C_BGREEN);
    api->kfmt(v, sizeof v, "(c) 2026 - built %s", api->os_build_date);
    api->draw_text(tx, cy + 48, v, C_SILVER);
}

static void sysinfo_draw(Win *w, int cx, int cy, int cw, int ch)
{
    static const char *const labels[7] = {
        "Processor", "Memory", "Screen", "Floppy", "USB storage", "Network", "Running for"
    };
    char b[96], hs[20];
    int x = cx + 10, y = cy + BANNER_H + 12;

    (void)w; (void)ch;
    g = gdi_bind(api, 11);
    banner(cx, cy, cw);
    for (int i = 0; i < 7; i++, y += 24) {
        if (!(i & 1)) api->fill_rect(cx + 8, y - 3, cw - 16, 22, C_G0 + 7);
        switch (i) {
        case 0: api->kfmt(b, sizeof b, "%s, %u MHz", api->cpu_brand(), api->cpu_mhz()); break;
        case 1:
            api->human_size_kb(api->mem_total_kb(), hs, sizeof hs);
            api->kfmt(b, sizeof b, "%s usable RAM", hs);
            break;
        case 2: api->kfmt(b, sizeof b, "%d x %d, 256 colours", *api->screen_w, *api->screen_h); break;
        case 3: api->strlcpy(b, "A: - 1.44 MB", sizeof b); break;
        case 4: api->strlcpy(b, api->usb_present() ? api->usb_model() : "No drive connected", sizeof b); break;
        case 5: {
            u32 ip = api->net_get(NET_IP);
            if (!api->net_up()) api->strlcpy(b, "No supported network card", sizeof b);
            else if (!ip) api->strlcpy(b, "Waiting for an address", sizeof b);
            else api->kfmt(b, sizeof b, "%u.%u.%u.%u (%s)", ip & 255, ip >> 8 & 255, ip >> 16 & 255,
                           ip >> 24, api->net_get(NET_DHCP_OK) ? "automatic" : "manual");
            break;
        }
        default: {
            u32 secs = *api->ticks / 100;
            api->kfmt(b, sizeof b, "%u h %u min %u sec", secs / 3600, secs / 60 % 60, secs % 60);
        }
        }
        api->draw_text(x, y, labels[i], C_G0 + 2);
        api->draw_text_clip(x + 110, y, b, C_BLACK, cw - 138);
    }
}

static void sysinfo_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 438;
    *h = BANNER_H + 184;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "System Info"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    g = gdi_bind(k, 11);
    static const AppDesc d = {
        .live_draw = APP_POINTER_FREE|APP_NO_CARET,
        .title = "System Info", .max_inst = 1, .in_menu = 1,
        .category = APP_CAT_SYSTEM,
        .draw = sysinfo_draw, .client_size = sysinfo_csize,
    };
    return k->register_app(&d) < 0;
}
