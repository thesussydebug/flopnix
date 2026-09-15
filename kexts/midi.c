#include "kapi.h"
#include "gdi.h"
#include "shpath.h"
#include "midiname.inc"
#include "ui.inc"
#include "opl2.h"
#include "midifile.inc"

static const Kapi *api;
static const GdiOps *gfx;
static const FmOps *fm;
static int my_type = -1;

#define WINW  470
#define WINH  318

#define MAXEV 16384

#define GA_Y  (UI_HDR + 10)
#define GA_H  76
#define GB_Y  (GA_Y + GA_H + 12)
#define GB_H  50
#define GC_Y  (GB_Y + GB_H + 12)
#define GC_H  76

typedef struct { u32 tick; u8 note, on; } Ev;
static Ev  evs[MAXEV];
static int nev;
static int div_ppqn;
static u32 us_per_qn = 500000;

static int  playing, cur;
static u32  play_t0;
static u32  song_ticks;
static u8   sounding[128];
static int  now_note = -1;
static char path[64], msg[56];
static u32  ev_dropped, drums_dropped;

static u8  v_note[FM_VOICES];
static u8  v_used[FM_VOICES];
static u32 v_age[FM_VOICES];
static u32 v_clock;

static void voices_reset(void)
{
    for (int i = 0; i < FM_VOICES; i++) { v_used[i] = 0; v_note[i] = 0; v_age[i] = 0; }
    v_clock = 0;
}

static int voice_count(void)
{
    int n = 0;
    for (int i = 0; i < FM_VOICES; i++) if (v_used[i]) n++;
    return n;
}

static void fm_start(int note, int vel)
{
    int pick = -1;
    for (int i = 0; i < FM_VOICES; i++)
        if (!v_used[i]) { pick = i; break; }
    if (pick < 0) {
        u32 oldest = 0xFFFFFFFFu;
        for (int i = 0; i < FM_VOICES; i++)
            if (v_age[i] < oldest) { oldest = v_age[i]; pick = i; }
    }
    if (pick < 0) return;
    v_note[pick] = (u8)note;
    v_used[pick] = 1;
    v_age[pick] = ++v_clock;
    fm->note_on(pick, note, vel);
}

static void fm_stop(int note)
{

    int pick = -1;
    u32 newest = 0;
    for (int i = 0; i < FM_VOICES; i++)
        if (v_used[i] && v_note[i] == note && v_age[i] >= newest) {
            newest = v_age[i];
            pick = i;
        }
    if (pick < 0) return;
    v_used[pick] = 0;
    fm->note_off(pick);
}

static void all_off(void)
{
    for (int i = 0; i < 128; i++) sounding[i] = 0;
    now_note = -1;
    if (fm) { fm->all_off(); voices_reset(); }
    api->speaker_off();
}

static void retune(void)
{
    int top = -1;
    for (int n = 127; n >= 0; n--)
        if (sounding[n]) { top = n; break; }
    if (top == now_note) return;
    now_note = top;
    if (top < 0) api->speaker_off();
    else {
        u32 hz = mf_note_hz(top);
        if (hz) api->speaker_tone(hz);
        else api->speaker_off();
    }
}

static void ev_add(u32 tick, int note, int on)
{
    if (nev >= MAXEV) { ev_dropped++; return; }
    evs[nev].tick = tick;
    evs[nev].note = (u8)note;
    evs[nev].on = (u8)on;
    nev++;
}

static int parse_midi(const u8 *p, int n)
{
    nev = 0;
    ev_dropped = 0;
    drums_dropped = 0;
    song_ticks = 0;
    us_per_qn = 500000;
    if (n <= 0) { api->strlcpy(msg, "cannot read that file", sizeof msg); return 0; }

    int ntrk = 0;
    u32 off = 0;
    div_ppqn = mf_header(p, (u32)n, &ntrk, &off);
    if (!div_ppqn) {
        api->strlcpy(msg, "not a MIDI file this can play (format 0 or 1 only)",
                     sizeof msg);
        return 0;
    }

    for (int t = 0; t < ntrk && off + 8 <= (u32)n; t++) {
        if (p[off] != 'M' || p[off + 1] != 'T' || p[off + 2] != 'r' ||
            p[off + 3] != 'k') break;
        u32 len = ((u32)p[off + 4] << 24) | ((u32)p[off + 5] << 16) |
                  ((u32)p[off + 6] << 8) | p[off + 7];
        u32 body = off + 8;
        if (body + len > (u32)n) len = (u32)n - body;

        MfTrack tr = { p + body, len, 0, 0, 0, 0 };
        u8 s, a, b;
        while (mf_next(&tr, &s, &a, &b)) {
            if (s == MF_META) {

                if (a == 0x51 && tr.i >= 3 && body + tr.i <= (u32)n) {
                    const u8 *q = p + body + tr.i - 3;
                    us_per_qn = ((u32)q[0] << 16) | ((u32)q[1] << 8) | q[2];
                }
                continue;
            }
            int kind = s & 0xF0;

            if ((s & 0x0F) == 9) {
                if (kind == 0x90 && b > 0) drums_dropped++;
                if (tr.tick > song_ticks) song_ticks = tr.tick;
                continue;
            }
            if (kind == 0x90 && b > 0)      ev_add(tr.tick, a, 1);
            else if (kind == 0x80 || kind == 0x90) ev_add(tr.tick, a, 0);
            if (tr.tick > song_ticks) song_ticks = tr.tick;
        }
        off = body + len;
    }

    if (!nev) {
        api->strlcpy(msg, "no playable notes in that file", sizeof msg);
        return 0;
    }

    for (int i = 1; i < nev; i++) {
        Ev k = evs[i];
        int j = i - 1;
        while (j >= 0 && evs[j].tick > k.tick) { evs[j + 1] = evs[j]; j--; }
        evs[j + 1] = k;
    }

    if (ev_dropped)
        api->kfmt(msg, sizeof msg, "loaded %d notes (%u dropped: song too long)",
                  nev, ev_dropped);
    else if (drums_dropped)
        api->kfmt(msg, sizeof msg, "loaded %d notes (%u percussion skipped)",
                  nev, drums_dropped);
    else
        api->kfmt(msg, sizeof msg, "loaded %d notes, %d ppqn", nev, div_ppqn);
    return 1;
}

static int load_midi(const char *spec)
{
    int drive;
    char file[96];
    if (!sh_spec_split(spec, &drive, file, sizeof file)) {
        drive = 0;
        api->strlcpy(file, spec, sizeof file);
    }
    api->buffer_lock();
    int n = drive == 1 ? api->fat_read(file, api->iobuf, api->iobuf_size)
                       : api->fs_read(file, api->iobuf, api->iobuf_size);
    int parsed=parse_midi(api->iobuf,n);api->buffer_unlock();
    if(!parsed)return 0;
    mn_display(spec, -1, path, sizeof path);
    return 1;
}

static void midi_tick(void *ctx)
{
    (void)ctx;
    if (!playing) return;
    u32 rate = mf_tickrate(us_per_qn, div_ppqn);
    if (!rate) { playing = 0; all_off(); return; }
    u32 elapsed = (u32)(*api->ticks - play_t0);
    u32 midi_now = elapsed * 1000 / rate;

    int fired = 0;
    while (cur < nev && evs[cur].tick <= midi_now) {
        int note = evs[cur].note, on = evs[cur].on;

        sounding[note] = (u8)on;
        if (fm) {
            if (on) fm_start(note, 100);
            else    fm_stop(note);
        }
        cur++;
        fired = 1;
    }
    if (fired && !fm) retune();
    if (cur >= nev) {
        playing = 0;
        all_off();
        api->strlcpy(msg, "finished", sizeof msg);
    }
    api->gui_dirty();
}

static void picked(const char *spec, void *ctx)
{
    (void)ctx;
    if (!spec || !spec[0]) return;
    playing = 0;
    all_off();
    load_midi(spec);
    cur = 0;
    api->gui_dirty();
}

static UiRect b_open(void) { return ui_r(22,       GA_Y + UI_GTOP, 92, UI_BTNH); }
static UiRect b_play(void) { return ui_r(22 + 100, GA_Y + UI_GTOP, 78, UI_BTNH); }
static UiRect b_stop(void) { return ui_r(22 + 186, GA_Y + UI_GTOP, 70, UI_BTNH); }

static void m_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    ui_header(cx, cy, cw, "MIDI Player");
    ui_header_right(cx, cy, cw, fm ? "FM synthesis" : "PC speaker",
                    fm ? C_GREEN : C_OLIVE);

    char t[80];
    int gw = cw - 24;

    ui_group(cx + 12, cy + GA_Y, gw, GA_H, "Song");
    ui_button(cx, cy, b_open(), "Open...", 0, 1);
    ui_button(cx, cy, b_play(), playing ? "Pause" : "Play", playing, nev > 0);
    ui_button(cx, cy, b_stop(), "Stop", 0, nev > 0);

    int x = ui_gx(cx + 12), y = cy + GA_Y + UI_GTOP + UI_BTNH + 8;
    api->draw_text_clip(x, y, path[0] ? path : "(no file loaded)",
                        path[0] ? C_BLACK : C_G0 + 3, gw - 20);
    y += 16;
    if (nev) {
        u32 pos = cur < nev ? evs[cur].tick : song_ticks;
        int pct = song_ticks ? (int)(pos * 100 / song_ticks) : 0;
        ui_progress(x, y, gw - 148, 14, pct, 1);
        api->kfmt(t, sizeof t, "%d of %d  %u bpm", cur, nev,
                  us_per_qn ? 60000000u / us_per_qn : 0);
        api->draw_text(x + gw - 140, y + 3, t, C_BLACK);
    }

    ui_group(cx + 12, cy + GB_Y, gw, GB_H, "Output");
    x = ui_gx(cx + 12);
    y = ui_gy(cy + GB_Y);
    if (fm) {
        api->kfmt(t, sizeof t, "%s at port %x", fm->name(), fm->port());
        ui_row(x, y, "Device", t, 80);
        api->kfmt(t, sizeof t, "%d voices, %d sounding", FM_VOICES, voice_count());
        ui_row(x, y + UI_ROWH, "Polyphony", t, 80);
    } else {
        ui_row(x, y, "Device", "PC speaker - no FM chip found at 0x388", 80);
        ui_row(x, y + UI_ROWH, "Polyphony",
               "1 note: the highest one held, shown in red", 80);
    }

    ui_group(cx + 12, cy + GC_Y, gw, GC_H, "Notes");
    int kx = ui_gx(cx + 12), kw = gw - 20;
    y = ui_gy(cy + GC_Y);
    api->panel(kx, y, kw, 30, 1);
    for (int n = 24; n < 96; n++) {
        int px = kx + 2 + (n - 24) * (kw - 4) / 72;
        u8 col = sounding[n] ? ((!fm && n == now_note) ? C_RED : C_BBLUE)
                             : C_G0 + 5;
        api->fill_rect(px, y + 3, (kw - 4) / 72 - 1, 24, col);
    }

    for (int n = 24; n < 96; n += 12) {
        int px = kx + 2 + (n - 24) * (kw - 4) / 72;
        api->kfmt(t, sizeof t, "C%d", n / 12 - 1);
        api->draw_text(px, y + 36, t, C_G0 + 3);
    }

    ui_status(cx, cy, cw, ch, msg[0] ? msg
                                     : "Open a .mid file from A: to play it.");
}

static void m_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    if (ui_hit(b_open(), lx, ly)) {
        api->file_picker("Choose a MIDI file", "mid", 0, picked, 0);
    } else if (ui_hit(b_play(), lx, ly)) {
        if (!nev) { api->strlcpy(msg, "open a .mid file first", sizeof msg); }
        else if (playing) { playing = 0; all_off(); }
        else {
            if (cur >= nev) cur = 0;

            u32 rate = mf_tickrate(us_per_qn, div_ppqn);
            u32 back = rate ? (cur < nev ? evs[cur].tick : 0) * rate / 1000 : 0;
            play_t0 = *api->ticks - back;
            playing = 1;
            msg[0] = 0;
        }
    } else if (ui_hit(b_stop(), lx, ly)) {
        playing = 0;
        cur = 0;
        all_off();
    }
    api->gui_dirty();
}

static int mid_opener(const char *name, const char *fullpath,
                      const u8 *data, int n)
{
    playing = 0;
    all_off();
    if (!parse_midi(data, n)) return -1;

    mn_display(fullpath ? fullpath : name, fullpath ? 1 : 0, path, sizeof path);
    cur = 0;
    return api->win_open(my_type) < 0 ? -1 : 0;
}

static void m_open(int inst) { (void)inst; }
static void m_close(int inst) { (void)inst; playing = 0; all_off(); }
static void m_csize(int inst, int *w, int *h) { (void)inst; *w = WINW; *h = WINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "MIDI Player"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    ui_init(k, gfx);

    fm = fm_bind(k, FM_ABI);
    voices_reset();
    static const AppDesc d = {.live_draw=APP_INDEPENDENT,
        .title = "MIDI Player", .max_inst = 1, .in_menu = 1, .resizable = 1,
        .open = m_open, .draw = m_draw, .mouse = m_mouse,
        .client_size = m_csize, .category = APP_CAT_PROGRAMS,
        .close = m_close,
    };
    my_type = k->register_app(&d);
    if (my_type < 0) return 1;
    k->register_opener("mid", mid_opener);
    k->register_opener("midi", mid_opener);
    k->timer_add(1, midi_tick, 0);
    return 0;
}
