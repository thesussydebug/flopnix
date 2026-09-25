#include "kapi.h"
#include "gdi.h"
#include "button.h"
#include "bench_core.inc"

static const Kapi *api;
static const GdiOps *gfx;

static char res[4][72];
static volatile int running;
static int failed;
static int phase, shell_wait;
static volatile int phase_ready;
static volatile u8 has_res;

#define TKS (*api->ticks)
#define SPAN 50

static u32 measure(int kind, u8 *scratch)
{
    volatile u32 acc = 1;
    int x = 0, y = 0, w = 320, h = 200;
    if (kind == 2) {
        api->clip_rect_get(&x, &y, &w, &h);
        if (w > 320) w = 320;
        if (h > 200) h = 200;
        if (w <= 0 || h <= 0) { failed = 1; return 0; }
        api->read_rect(x, y, w, h, scratch, w);
    }

    if (kind == 1) api->memcpy(scratch + 16384, scratch, 16384);
    u32 start = TKS, last = start, count = 0, stuck = 0;
    do {
        if (kind == 0) {
            for (u32 i = 0; i < 4096; i++) acc += i;
        } else if (kind == 1) api->memcpy(scratch + 16384, scratch, 16384);
        else api->fill_rect(x, y, w, h, (u8)(count & 15));
        count++;
        u32 now = TKS;
        if (bm_stalled(now, &last, &stuck, 65536)) { failed = 1; break; }
    } while ((u32)(TKS - start) < SPAN);
    u32 elapsed = TKS - start;
    if (kind == 2) api->blit(x, y, w, h, scratch, w);
    (void)acc;
    if (!elapsed) { failed = 1; return 0; }
    if (kind == 0) return bm_muldiv(count, 409600, elapsed * 1000);
    if (kind == 1) return bm_muldiv(count, 1600, elapsed);
    return bm_muldiv(count, (u32)w * h, elapsed * 10);
}

static u32 median_measure(int kind)
{
    u32 bytes = kind == 1 ? 32768u : kind == 2 ? 320u * 200u : 0;
    u8 *scratch = bytes ? api->kmalloc(bytes) : 0;
    if (bytes && !scratch) { failed = 2; return 0; }
    if (scratch) api->mem_track("Benchmark scratch", scratch, bytes);
    if (kind == 1)
        for (u32 i = 0; i < 16384; i++) scratch[i] = (u8)(i * 37 + 11);
    u32 a = measure(kind, scratch), b = measure(kind, scratch), c = measure(kind, scratch);
    api->kfree(scratch);
    return bm_median(a, b, c);
}

static int run_timer = -1;

static void run_all(void);
static void schedule_run(void);

static void run_tick(void *ctx)
{
    (void)ctx;
    if (run_timer >= 0) { api->timer_del(run_timer); run_timer = -1; }
    run_all();
    if (has_res) return;
    if (shell_wait) phase_ready = 1;
    else schedule_run();
}

static void schedule_run(void)
{
    run_timer = api->timer_add(1, run_tick, 0);
    if (run_timer < 0) {
        api->strlcpy(res[0], "Unable to schedule benchmark", sizeof res[0]);
        res[1][0] = res[2][0] = res[3][0] = 0;
        has_res = 1; running = 0;
    }
    api->gui_dirty();
}

static void run_request(int from_shell)
{
    if (running) return;
    running = 1;
    has_res = 0;
    phase = phase_ready = failed = 0;
    shell_wait = from_shell;
    schedule_run();
}

static void run_all(void)
{
    if (!*api->timer_alive) {
        api->strlcpy(res[0], "no timer - cannot measure", sizeof res[0]);
        res[1][0] = res[2][0] = res[3][0] = 0;
        has_res = 1; running = 0;
        return;
    }
    if (phase == 0) {
        api->kfmt(res[0], sizeof res[0], "%s | %u MHz (guest)",
                  api->cpu_brand(), api->cpu_mhz());
    }
    u32 n = median_measure(phase);
    if (phase == 0)
        api->kfmt(res[1], sizeof res[1], "Calculations  %u.%02u M additions/s", n / 1000, n % 1000 / 10);
    else if (phase == 1)
        api->kfmt(res[2], sizeof res[2], "Memory copies %u.%02u MiB/s", n / 1024, n % 1024 * 100 / 1024);
    else
        api->kfmt(res[3], sizeof res[3], "Drawing       %u.%02u Mpixels/s", n / 1000, n % 1000 / 10);
    if (failed) {
        api->strlcpy(res[1], failed == 2 ? "Not enough memory to run benchmark" : "Measurement interrupted / clock stalled", sizeof res[1]);
        res[2][0] = res[3][0] = 0;
    }
    if (++phase < 3 && !failed) return;
    running = 0;
    has_res = 1;
    api->gui_dirty();
}

static void bench_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    api->fill_rect(cx, cy, cw, ch, C_FACE);
    if (gfx) { gfx->set_dither(1); gfx->fill_gradient(cx, cy, cw, 32, GRGB(214, 218, 228), GRGB(180, 186, 202), 1); gfx->set_dither(0); }
    api->draw_text(cx + 10, cy + 8, "Computer speed", C_NAVY);
    (void)w;button_label(api,cx+cw-62,cy+6,52,21,"Run",0,!running);

    if (!has_res) {
        api->draw_text(cx + 10, cy + 40, running ? "Measuring..." : "Simple benchmark", C_G0 + 3);
        return;
    }
    for (int i = 0; i < 4; i++)
        if (res[i][0])
            api->draw_text_clip(cx + 10, cy + 40 + i * 20, res[i],
                                i ? C_BLACK : C_G0 + 3, cw - 20);
}

static void bench_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)ch;
    if (ev != EV_PRESS) return;
    if (lx >= cw - 62 && lx < cw - 10 && ly >= 6 && ly < 27) run_request(0);
}

static void bench_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 380;
    *h = 164;
}

static void cmd_bench(const char *args)
{
    (void)args;
    if (running) { api->shell_print("bench: already running\n"); return; }
    api->shell_print("benchmarking (about 5 seconds)...\n");

    run_request(1);
    u32 t0 = *api->ticks;
    u32 last = t0, guard = 0;
    while (!has_res && (u32)(*api->ticks - t0) < 1500) {
        if (phase_ready) {
            phase_ready = 0;
            api->gui_pump();
            schedule_run();
        }
        if (bm_stalled(*api->ticks, &last, &guard, 50000000u)) break;
    }
    if (!has_res) {
        shell_wait = 0;
        if (phase_ready) { phase_ready = 0; schedule_run(); }
        api->shell_print("bench: no result (timer stalled)\n"); return;
    }
    for (int i = 0; i < 4; i++)
        if (res[i][0]) {
            api->shell_print(res[i]);
            api->shell_print("\n");
        }
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Benchmark"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {
        .title = "Benchmark", .max_inst = 1, .in_menu = 1,
        .draw = bench_draw, .mouse = bench_mouse, .client_size = bench_csize,
    };

    if (k->register_app(&d) < 0) return 1;
    k->register_cmd("bench", "bench - measure CPU / memory / graphics speed",
                    cmd_bench);
    return 0;
}
