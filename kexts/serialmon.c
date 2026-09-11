#include "kapi.h"
#include "gdi.h"
#include "serial_core.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1, timer_id = -1;

#define COM1 0x3F8
#define COLS 62
#define ROWS 18
#define HDR  30

static char scr[ROWS][COLS + 1];
static int  cx_, cy_;
static u32  baud = 115200;
static int  connected;
static u32  rx_bytes, tx_bytes;
static int  err_seen;
static int  local_echo = 1;

static const u32 RATES[4] = { 9600, 19200, 57600, 115200 };

static volatile int uart_setup;

static void uart_open(void)
{
    int div = uart_divisor(baud);
    if (!div) { connected = 0; return; }
    uart_setup = 1;
    api->outb(COM1 + UART_IER, 0x00);
    api->outb(COM1 + UART_LCR, LCR_DLAB);
    api->outb(COM1 + UART_DLL, (u8)(div & 0xFF));
    api->outb(COM1 + UART_DLM, (u8)(div >> 8));
    api->outb(COM1 + UART_LCR, LCR_8N1);
    api->outb(COM1 + UART_FCR, 0xC7);
    api->outb(COM1 + UART_MCR, 0x0B);

    api->outb(COM1 + 7, 0x5A);
    connected = api->inb(COM1 + 7) == 0x5A;
    uart_setup = 0;
}

static void put_ch(char c);

static void uart_tx(char c)
{
    if (!connected) return;
    for (int guard = 0; guard < 100000; guard++)
        if (api->inb(COM1 + UART_LSR) & LSR_THRE) {
            api->outb(COM1 + UART_THR, (u8)c);
            tx_bytes++;
            return;
        }
}

static void scroll_up(void)
{

    cy_ = ROWS - 1;
    for (int y = 0; y < ROWS - 1; y++)
        api->memcpy(scr[y], scr[y + 1], COLS + 1);
    api->memset(scr[ROWS - 1], 0, COLS + 1);
}

static void put_ch(char c)
{
    if (c == '\r') { cx_ = 0; return; }
    if (c == '\n') { cx_ = 0; if (++cy_ >= ROWS) scroll_up(); return; }
    if (c == '\b') { if (cx_ > 0) scr[cy_][--cx_] = 0; return; }
    if (c < 32 || c > 126) c = '.';
    scr[cy_][cx_++] = c;
    if (cx_ >= COLS) { cx_ = 0; if (++cy_ >= ROWS) scroll_up(); }

    if (cy_ < ROWS && cx_ <= COLS) scr[cy_][cx_] = 0;
}

static int sm_is_open(void)
{
    if (my_type < 0) return 0;
    int n = api->win_max();
    for (int i = 0; i < n; i++) {
        const Win *w = api->win_slot(i);
        if (w && w->used && w->type == (u8)my_type) return 1;
    }
    return 0;
}

static void poll(void *ctx)
{
    (void)ctx;
    if (uart_setup) return;
    if (!connected || !sm_is_open()) return;
    int got = 0;
    for (int i = 0; i < 64; i++) {
        if (uart_setup) break;

        u8 lsr = api->inb(COM1 + UART_LSR);
        if (uart_lsr_error(lsr)) err_seen = 1;
        if (!(lsr & LSR_DR)) break;
        put_ch((char)api->inb(COM1 + UART_RBR));
        rx_bytes++;
        got = 1;
    }
    if (got) api->gui_dirty();
}

static void sm_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w;
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    if (gfx) {
        gfx->set_dither(1);
        gfx->fill_gradient(cx, cy, cw, HDR - 4, GRGB(222, 226, 236),
                           GRGB(190, 196, 202), 1);
        gfx->set_dither(0);
    }
    api->hline(cx, cy + HDR - 4, cw, C_SHAD);

    for (int i = 0; i < 4; i++) {
        int bx = cx + 6 + i * 58;
        int on = RATES[i] == baud;
        api->panel(bx, cy + 4, 54, 18, on);
        if (on) api->fill_rect(bx + 2, cy + 6, 50, 14, C_HILITE);
        char b[12];
        api->kfmt(b, sizeof b, "%u", RATES[i]);
        api->draw_text(bx + 5, cy + 7, b, on ? C_WHITE : C_NAVY);
    }
    api->panel(cx + 244, cy + 4, 56, 18, local_echo);
    api->draw_text(cx + 250, cy + 7, "echo", local_echo ? C_NAVY : C_GRAY);

    char st[40];
    api->kfmt(st, sizeof st, "%s rx %u tx %u%s",
              connected ? "COM1" : "no UART", rx_bytes, tx_bytes,
              err_seen ? " ERR" : "");
    api->draw_text_clip(cx + 306, cy + 7, st, connected ? C_NAVY : C_MAROON,
                        cw - 312);

    int ty = cy + HDR;
    api->fill_rect(cx + 4, ty, cw - 8, ch - HDR - 4, C_TERMBG);
    for (int y = 0; y < ROWS; y++) {
        int py = ty + 2 + y * 12;
        if (py + 12 > cy + ch - 4) break;
        api->draw_text_clip(cx + 8, py, scr[y], C_TERMFG, cw - 16);
    }
    int cypx = ty + 2 + cy_ * 12;
    if (cypx + 12 <= cy + ch - 4)
        api->fill_rect(cx + 8 + cx_ * 8, cypx + 10, 7, 2, C_TERMFG);
}

static void sm_key(int inst, int k)
{
    (void)inst;
    if (k == '\n' || k == '\r') { uart_tx('\r'); uart_tx('\n'); if (local_echo) put_ch('\n'); }
    else if (k == '\b')         { uart_tx('\b'); if (local_echo) put_ch('\b'); }
    else if (k >= 32 && k < 127) { uart_tx((char)k); if (local_echo) put_ch((char)k); }
    else return;
    api->gui_dirty();
}

static void sm_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;

    if (ev != EV_PRESS || ly < 4 || ly >= 22) return;
    for (int i = 0; i < 4; i++)
        if (lx >= 6 + i * 58 && lx < 6 + i * 58 + 54) {
            baud = RATES[i];
            uart_open();
            api->gui_dirty();
            return;
        }
    if (lx >= 244 && lx < 300) { local_echo = !local_echo; api->gui_dirty(); }
}

static void sm_open(int inst)
{
    (void)inst;
    api->memset(scr, 0, sizeof scr);
    cx_ = cy_ = 0;
    rx_bytes = tx_bytes = 0;
    err_seen = 0;
    uart_open();
    if (!connected) {
        for (const char *s = "no UART detected on COM1"; *s; s++) put_ch(*s);
        put_ch('\n');
    }
    if (timer_id < 0) timer_id = api->timer_add(1, poll, 0);
}

static void sm_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = COLS * 8 + 16;
    *h = ROWS * 12 + HDR + 8;
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Serial Monitor"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Serial Monitor", .max_inst = 1, .in_menu = 1,
        .open = sm_open, .draw = sm_draw, .key = sm_key, .mouse = sm_mouse,
        .client_size = sm_csize, .category = APP_CAT_DEV,
    };
    my_type = k->register_app(&d);
    return my_type < 0;
}
