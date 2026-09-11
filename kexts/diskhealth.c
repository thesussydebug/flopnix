#include "kapi.h"
#include "gdi.h"
#include "ui.inc"
#include "fdchealth.inc"
#include "fhlayout.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1;

#define WINW  536
#define WINH  390

static u8  map[FH_CYLS][FH_HEADS];
static u32 n_ok, n_slow, n_retry, n_bad;
static u32 scan_lba;
static u8  scanning, done_once, aborted;
static u32 seek_ms[8];
static char msg[52];

static void reset_scan(void)
{
    for (int c = 0; c < FH_CYLS; c++)
        for (int h = 0; h < FH_HEADS; h++) map[c][h] = FH_UNTESTED;
    n_ok = n_slow = n_retry = n_bad = 0;
    scan_lba = 0;
    aborted = 0;

    done_once = 0;
}

static u32 next_chunk;

static void scan_chunk(void)
{
    u8 buf[512];
    u32 c0 = *api->ticks;
    for (int i = 0; i < 18 && scan_lba < FH_TOTAL; i++) {
        if ((u32)(*api->ticks - c0) >= 4) break;
        u32 lba = scan_lba++;
        u32 t0 = *api->ticks;
        int rc = api->disk_read(lba, buf);
        u32 ms = (u32)(*api->ticks - t0) * 10;

        int tries = (int)api->disk_stat(DS_LAST_TRIES);
        int cls = fh_class(rc, tries, ms);

        int c, h;
        if (fh_cell(lba, &c, &h)) map[c][h] = (u8)fh_worst(map[c][h], cls);
        if (cls == FH_BAD)        n_bad++;
        else if (cls == FH_RETRY) n_retry++;
        else if (cls == FH_SLOW)  n_slow++;
        else                      n_ok++;
    }

    next_chunk = *api->ticks + 8;
    if (scan_lba >= FH_TOTAL) {
        scanning = 0;
        done_once = 1;
        api->kfmt(msg, sizeof msg, "scan complete: %u ok, %u slow, %u retried, "
                  "%u bad", n_ok, n_slow, n_retry, n_bad);
        char t[72];
        api->kfmt(t, sizeof t, "floppy scan: %u ok %u slow %u retry %u bad",
                  n_ok, n_slow, n_retry, n_bad);
        api->ktrace(t);
    }
    api->gui_dirty();
}

static void seek_test(void)
{
    u8 buf[512];
    for (int i = 0; i < 8; i++) {
        u32 cyl = (u32)i * (FH_CYLS - 1) / 7;
        api->disk_read(0, buf);
        u32 t0 = *api->ticks;
        api->disk_read(cyl * FH_HEADS * FH_SECTORS, buf);
        seek_ms[i] = (u32)(*api->ticks - t0) * 10;
        api->gui_dirty();
    }
    api->strlcpy(msg, "seek timing done", sizeof msg);
}

static int fh_win_open(void)
{
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == my_type) return 1;
    }
    return 0;
}

static void fh_tick(void *ctx)
{
    (void)ctx;
    if (!scanning) return;

    if (!fh_win_open()) { scanning = 0; aborted = 1; return; }

    if (api->esc_pending()) {
        scanning = 0;
        aborted = 1;
        api->strlcpy(msg, "scan aborted - the map shows what was read",
                     sizeof msg);
        api->gui_dirty();
        return;
    }
    if ((i32)(*api->ticks - next_chunk) < 0) return;
    scan_chunk();
}

static void fh_key(int inst, int k)
{
    (void)inst;
    if (k == 27 && scanning) {
        scanning = 0;
        aborted = 1;
        api->strlcpy(msg, "scan aborted - the map shows what was read",
                     sizeof msg);
    }
}

static void fh_close(int inst)
{
    (void)inst;
    if (scanning) {
        scanning = 0;
        aborted = 1;
        api->strlcpy(msg, "scan stopped - window closed", sizeof msg);
    }
}

static UiRect btn_scan(int ch)  { return ui_r(20,  fh_layout(WINW, ch).buttons_y, 104, UI_BTNH); }
static UiRect btn_seek(int ch)  { return ui_r(132, fh_layout(WINW, ch).buttons_y, 96, UI_BTNH); }
static UiRect btn_reset(int ch) { return ui_r(236, fh_layout(WINW, ch).buttons_y, 56, UI_BTNH); }

static u8 cls_colour(int cls)
{
    switch (cls) {
    case FH_OK:    return C_BGREEN;
    case FH_SLOW:  return C_YELLOW;
    case FH_RETRY: return C_OLIVE;
    case FH_BAD:   return C_RED;
    default:       return C_G0 + 2;
    }
}

static void fh_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w;
    FhLayout l = fh_layout(cw, ch);
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    ui_header(cx, cy, cw, "Floppy Health");
    int verdict = fh_verdict(n_bad, n_retry, n_slow, n_ok + n_slow + n_retry + n_bad);
    const char *state = scanning ? "Scanning" : aborted ? "Partial" :
                        done_once ? (verdict == 0 ? "Healthy" : verdict == 1 ? "Aging" : "Failing") : "Drive A:";
    ui_header_right(cx, cy, cw, state, done_once && verdict == 2 ? C_RED : C_NAVY);
    char t[96];
    if (!l.tiny) {
        ui_group(cx + 12, cy + l.activity_y, cw - 24, l.activity_h, "Drive activity since boot");
        const char *labels[3] = {"Operations", "Retried", "Failed"};
        const int stats[3] = {DS_OPS, DS_RETRIED, DS_FAILED};
        int col = (cw - 40) / 3;
        for (int i = 0; i < 3; i++) {
            int x = cx + 20 + col * i;
            api->draw_text_clip(x, cy + l.activity_y + 10, labels[i], C_G0 + 2, col - 6);
            api->kfmt(t, sizeof t, "%u", api->disk_stat(stats[i]));
            api->draw_text_clip(x, cy + l.activity_y + 26, t, C_BLACK, col - 6);
        }
        if (l.activity_h > 44) {
            api->kfmt(t, sizeof t, "Last %u ms / %u tries    Slowest %u ms",
                      api->disk_stat(DS_LAST_MS), api->disk_stat(DS_LAST_TRIES), api->disk_stat(DS_WORST_MS));
            api->draw_text_clip(cx + 20, cy + l.activity_y + 46, t, C_G0 + 2, cw - 40);
            api->kfmt(t, sizeof t, "ST0 %02x  ST1 %02x  ST2 %02x    LBA %u",
                      api->disk_stat(DS_ST0), api->disk_stat(DS_ST1), api->disk_stat(DS_ST2), api->disk_stat(DS_LAST_LBA));
            api->draw_text_clip(cx + 20, cy + l.activity_y + 64, t, C_G0 + 2, cw - 40);
        }
        ui_group(cx + 12, cy + l.surface_y, cw - 24, l.status_y - l.surface_y - 6, "Surface scan");
    }
    ui_button(cx, cy, btn_scan(ch), scanning ? "Abort scan" : "Scan surface", scanning, 1);
    ui_button(cx, cy, btn_seek(ch), "Time seeks", 0, !scanning);
    ui_button(cx, cy, btn_reset(ch), "Clear", 0, 1);
    if (!l.tiny) api->draw_text(cx + l.map_x, cy + l.map_y - 16, "80 cylinders / 2 heads", C_G0 + 2);
    api->panel(cx + l.map_x - 2, cy + l.map_y - 2, l.map_w + 4, l.map_h + 4, 1);
    for (int c = 0; c < FH_CYLS; c++)
        for (int h = 0; h < FH_HEADS; h++) {
            int x0 = c * l.map_w / FH_CYLS, x1 = (c + 1) * l.map_w / FH_CYLS;
            int y0 = h * l.map_h / FH_HEADS, y1 = (h + 1) * l.map_h / FH_HEADS;
            api->fill_rect(cx + l.map_x + x0, cy + l.map_y + y0,
                           x1 - x0 - 1, y1 - y0 - 1, cls_colour(map[c][h]));
        }
    if (!l.tiny) {
        for (int c = 0; c < 80; c += l.narrow ? 20 : 10) {
            int x = cx + l.map_x + c * l.map_w / FH_CYLS;
            api->vline(x, cy + l.ticks_y, 3, C_SHAD);
            api->kfmt(t, sizeof t, "%d", c);
            api->draw_text(x, cy + l.ticks_y + 3, t, C_G0 + 2);
        }
    }
    const char *names[4] = {"OK", "Slow", "Retry", "Bad"};
    u32 counts[4] = {n_ok, n_slow, n_retry, n_bad};
    int classes[4] = {FH_OK, FH_SLOW, FH_RETRY, FH_BAD};
    int cols = l.narrow ? 2 : 4;
    for (int i = 0; i < 4; i++) {
        int x = cx + l.map_x + (i % cols) * (l.map_w / cols);
        int y = cy + l.legend_y + (i / cols) * 18;
        api->fill_rect(x, y + 3, 8, 8, cls_colour(classes[i]));
        api->kfmt(t, sizeof t, "%s %u", names[i], counts[i]);
        api->draw_text_clip(x + 13, y, t, C_BLACK, l.map_w / cols - 18);
    }
    if (!l.tiny) {
        if (seek_ms[7] || seek_ms[0]) {
            u32 lo = seek_ms[0], hi = lo, sum = 0;
            for (int i = 0; i < 8; i++) {
                if (seek_ms[i] < lo) lo = seek_ms[i];
                if (seek_ms[i] > hi) hi = seek_ms[i];
                sum += seek_ms[i];
            }
            api->kfmt(t, sizeof t, "Seek: %u-%u ms, avg %u", lo, hi, sum / 8);
        } else api->strlcpy(t, "Seek timing: not measured", sizeof t);
        api->draw_text_clip(cx + 20, cy + l.seek_y, t, C_G0 + 2, cw - 40);
        if (scanning || done_once || aborted)
            ui_progress(cx + 20, cy + l.progress_y, cw - 40, 16,
                        (int)(scan_lba * 100 / FH_TOTAL), 1);
        else api->draw_text_clip(cx + 20, cy + l.progress_y, "Read-only scan / no sector remapping", C_G0 + 2, cw - 40);
    }
    if (scanning) api->kfmt(t, sizeof t, "Reading %u / %d sectors - Esc stops", scan_lba, FH_TOTAL);
    else if (aborted) api->strlcpy(t, "Partial scan - untested areas remain", sizeof t);
    else if (done_once) api->strlcpy(t, verdict == 2 ? "Failing sectors - replace this disk" : verdict == 1 ? "Read errors or delays - back up this disk" : "Full scan complete - all sectors read", sizeof t);
    else api->strlcpy(t, msg[0] ? msg : "Grey cells have not been scanned", sizeof t);
    ui_status(cx, cy, cw, ch, t);
}

static void fh_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    if (ui_hit(btn_scan(ch), lx, ly)) {
        if (scanning) {
            scanning = 0;
            aborted = 1;
            api->strlcpy(msg, "scan aborted - the map shows only what was read",
                         sizeof msg);
        } else {
            reset_scan();
            api->esc_arm();
            scanning = 1;
            msg[0] = 0;
        }
        api->gui_dirty();
    } else if (ui_hit(btn_seek(ch), lx, ly)) {
        if (!scanning) seek_test();
        api->gui_dirty();
    } else if (ui_hit(btn_reset(ch), lx, ly)) {
        scanning = 0;
        done_once = 0;
        reset_scan();
        for (int i = 0; i < 8; i++) seek_ms[i] = 0;
        msg[0] = 0;
        api->gui_dirty();
    }
}

static void fh_open(int inst) { (void)inst; reset_scan(); msg[0] = 0; }
static void fh_min(int *w, int *h) { *w = 312; *h = 330; }
static void fh_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Floppy Health"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    ui_init(k, gfx);
    reset_scan();
    static const AppDesc d = {
        .title = "Floppy Health", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = fh_open, .draw = fh_draw, .mouse = fh_mouse,
        .client_size = fh_csize, .min_client = fh_min, .category = APP_CAT_SYSTEM,
        .close = fh_close, .key = fh_key,
    };
    my_type = k->register_app(&d);
    if (my_type < 0) return 1;
    k->timer_add(5, fh_tick, 0);
    return 0;
}
