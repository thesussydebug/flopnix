#include "kapi.h"
#include "gdi.h"
#include "ui.inc"

static const Kapi *api;
static const GdiOps *gfx;

#define FIX   10000
#define VMAX  999999999LL
#define FMAX  (VMAX * FIX)

static long long divll(long long n, long long d)
{
    if (d == 0) return 0;
    int neg = (n < 0) ^ (d < 0);
    unsigned long long un = n < 0 ? -(unsigned long long)n : (unsigned long long)n;
    unsigned long long ud = d < 0 ? -(unsigned long long)d : (unsigned long long)d;
    unsigned long long q = 0, rem = 0;
    for (int i = 0; i < 64; i++) {
        rem = (rem << 1) | (un >> 63);
        un <<= 1;
        q <<= 1;
        if (rem >= ud) { rem -= ud; q |= 1; }
    }
    return neg ? -(long long)q : (long long)q;
}

static int mul_ovf(long long a, long long b, long long *out)
{
    long long p = a * b;
    if (a != 0 && divll(p, a) != b) return 1;
    *out = p;
    return 0;
}

static long long acc;
static char entry[24];
static int  elen;
static char pend;
static u8   err;

static long long entry_val(void)
{
    long long ip = 0, fp = 0, scale = FIX;
    int neg = 0, dot = 0;
    for (int i = 0; i < elen; i++) {
        char c = entry[i];
        if (c == '-') neg = 1;
        else if (c == '.') dot = 1;
        else if (!dot) ip = ip * 10 + (c - '0');
        else if (scale > 1) { scale /= 10; fp += (c - '0') * scale; }
    }
    long long v = ip * FIX + fp;
    return neg ? -v : v;
}

static void fmt_fix(long long v, char *out, int cap)
{
    if (err) { api->strlcpy(out, "error", cap); return; }
    long long a = v < 0 ? -v : v;
    long long ipl = divll(a, FIX);
    u32 ip = (u32)ipl;
    u32 fp = (u32)(a - ipl * FIX);
    if (!fp) {
        api->kfmt(out, cap, "%s%u", v < 0 ? "-" : "", ip);
        return;
    }
    char frac[8];
    api->kfmt(frac, sizeof frac, "%04u", fp);
    for (int i = 3; i > 0 && frac[i] == '0'; i--) frac[i] = 0;
    api->kfmt(out, cap, "%s%u.%s", v < 0 ? "-" : "", ip, frac);
}

static void apply(void)
{
    long long b = elen ? entry_val() : acc;
    long long r;
    switch (pend) {
    case '+': r = acc + b; break;
    case '-': r = acc - b; break;
    case '*': {
        long long p;
        if (mul_ovf(acc, b, &p)) { err = 1; return; }
        r = divll(p, FIX);
        break;
    }
    case '/':
        if (b == 0) { err = 1; return; }
        {
            long long num;
            if (mul_ovf(acc, FIX, &num)) { err = 1; return; }
            r = divll(num, b);
        }
        break;
    default: r = b; break;
    }
    if (r > FMAX || r < -FMAX) { err = 1; return; }
    acc = r;
}

static void press(char k)
{
    if (k == 'C') { acc = 0; elen = 0; entry[0] = 0; pend = 0; err = 0; return; }
    if (err) return;
    if (k == 'E') { elen = 0; entry[0] = 0; return; }
    if (k == '\b') { if (elen) entry[--elen] = 0; return; }
    if (k >= '0' && k <= '9') {
        int dot = -1, digs = 0;
        for (int i = 0; i < elen; i++) {
            if (entry[i] == '.') dot = i;
            else if (entry[i] != '-') digs++;
        }

        int idigs = dot < 0 ? digs : dot - (entry[0] == '-' ? 1 : 0);
        if (idigs >= 9) return;
        if (dot >= 0 && elen - dot > 4) return;
        if (elen < 22) { entry[elen++] = k; entry[elen] = 0; }
        return;
    }
    if (k == '.') {
        for (int i = 0; i < elen; i++) if (entry[i] == '.') return;
        if (elen < 20) {
            if (!elen) entry[elen++] = '0';
            entry[elen++] = '.';
            entry[elen] = 0;
        }
        return;
    }
    if (k == '~') {
        if (elen && entry[0] == '-') { api->memmove(entry, entry + 1, elen--); }
        else if (elen && elen < 22) { api->memmove(entry + 1, entry, ++elen); entry[0] = '-'; }
        else if (!elen) acc = -acc;
        return;
    }
    if (k == '+' || k == '-' || k == '*' || k == '/') {

        if (!elen && pend) { pend = k; return; }
        if (elen || pend) apply();
        pend = err ? 0 : k;
        elen = 0;
        entry[0] = 0;
        return;
    }
    if (k == '=') {
        apply();
        pend = 0;
        elen = 0;
        entry[0] = 0;
    }
}

#define BW  36
#define BH  24
#define GAP 4
static const char *const grid[5][4] = {
    { "C", "CE", "<-", "/" },
    { "7", "8",  "9",  "*" },
    { "4", "5",  "6",  "-" },
    { "1", "2",  "3",  "+" },
    { "0", "+/-", ".", "=" },
};

static char btn_key(int r, int c)
{
    const char *s = grid[r][c];
    if (s[1] == 0) return s[0];
    if (s[0] == 'C') return 'E';
    if (s[0] == '<') return '\b';
    return '~';
}

static void calc_csize(int inst, int *w, int *h)
{
    (void)inst;
    *w = 4 * (BW + GAP) - GAP + 16;
    *h = 24 + 12 + 5 * (BH + GAP) - GAP + 16;
}

static void calc_draw(Win *w, int cx, int cy, int cw, int ch)
{
    gfx = gdi_bind(api, 11);
    (void)w; (void)ch;
    if (gfx) { gfx->set_dither(1); gfx->fill_gradient(cx, cy, cw, 40, GRGB(214, 218, 228), GRGB(180, 186, 202), 1); gfx->set_dither(0); }
    char d[24];
    if (elen) api->strlcpy(d, entry, sizeof d);
    else fmt_fix(acc, d, sizeof d);
    api->panel(cx + 8, cy + 8, cw - 16, 24, 1);
    api->fill_rect(cx + 10, cy + 10, cw - 20, 20, C_WHITE);
    int dl = (int)api->strlen(d);
    api->draw_text(cx + cw - 14 - dl * 8, cy + 12, d, C_BLACK);
    if (pend && !elen) api->draw_char(cx + 12, cy + 12, pend, C_G0 + 4);

    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            int bx = cx + 8 + c * (BW + GAP), by = cy + 40 + r * (BH + GAP);
            button_label(api,bx,by,BW,BH,grid[r][c],0,1);
        }
}

static void calc_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)inst; (void)cw; (void)ch;
    ui_pointer(lx,ly,ev);
    if (ev != EV_RELEASE) return;

    if (lx < 8 || ly < 40) return;
    int c = (lx - 8) / (BW + GAP), r = (ly - 40) / (BH + GAP);
    if (c < 0 || c > 3 || r < 0 || r > 4) return;
    if (lx - 8 - c * (BW + GAP) >= BW || ly - 40 - r * (BH + GAP) >= BH) return;
    if(ui_click(ui_r(8+c*(BW+GAP),40+r*(BH+GAP),BW,BH),lx,ly,ev))press(btn_key(r,c));
}

static void calc_key(int inst, int k)
{
    (void)inst;
    if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*' ||
        k == '/' || k == '.' || k == '\b') press((char)k);
    else if (k == '\n' || k == '=') press('=');
    else if (k == 'c' || k == 'C' || k == 27) press('C');
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Calculator"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    static const AppDesc d = {.live_draw=APP_INDEPENDENT,
        .title = "Calculator", .max_inst = 1, .in_menu = 1,
        .draw = calc_draw, .key = calc_key, .mouse = calc_mouse,
        .client_size = calc_csize,
    };
    return k->register_app(&d) < 0;
}
