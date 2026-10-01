enum { PD_WAIT, PD_LINK, PD_SEND, PD_DONE, PD_PART };
enum { PD_DIM, PD_ON, PD_OFF };
#define PD_RESTART_BEATS 6
typedef struct { u32 sent, total, seq; int state, beats; } PdState;

static char *pd_num(char *p, u32 v)
{
    char t[16];
    int n = 0;
    do {
        if (n % 4 == 3) t[n++] = ',';
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static char *pd_put(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

static u32 pd_part(u32 done, u32 total, u32 scale)
{
    if (!total) return 0;
    if (done >= total) return scale;
    while (total > 0x3fffffu) { total >>= 1; done >>= 1; }
    u32 r = done * scale / total;
    return r < scale ? r : scale - 1;
}

static void pd_lines(char l[3][40], const PdState *d, u32 ip)
{
    char *p = l[1];
    pd_put(l[2], d->state >= PD_DONE ? "Restarting..." : "Please wait...");
    if (d->state == PD_WAIT) {
        pd_put(l[0], "Waiting for Manager");
        p = pd_put(p, "at ");
        for (int i = 0; i < 4; i++) {
            p = pd_num(p, (ip >> (8 * i)) & 255);
            if (i < 3) p = pd_put(p, ".");
        }
        return;
    }
    if (d->state == PD_LINK) {
        pd_put(l[0], "0% complete.");
        pd_put(p, "Preparing dump...");
        return;
    }
    if (d->state == PD_PART) {
        pd_put(l[0], "Dump incomplete.");
    } else {
        u32 pct = d->state == PD_DONE ? 100 : pd_part(d->sent, d->total, 100);
        if (d->state != PD_DONE && pct > 99) pct = 99;
        pd_put(pd_num(l[0], pct), "% complete.");
    }
    p = pd_num(p, d->sent);
    p = pd_put(p, " / ");
    p = pd_num(p, d->total);
    pd_put(p, " bytes");
}

static u32 pd_fill(const PdState *d)
{
    if (d->state == PD_DONE) return 1000;
    if (d->state < PD_SEND) return 0;
    u32 f = pd_part(d->sent, d->total, 1000);
    return d->state == PD_SEND && f > 999 ? 999 : f;
}

static int pd_lit(const PdState *d, int blink)
{
    if (d->state == PD_WAIT) return PD_DIM;
    if (d->state >= PD_DONE) return PD_ON;
    return blink ? PD_ON : PD_OFF;
}

static void pd_note(PdState *d, u32 op, u32 seq, u32 address, u32 length, int ok)
{
    if (d->state >= PD_DONE) return;
    if (d->state == PD_WAIT) d->state = PD_LINK;
    if (op == 6 && ok && d->state == PD_SEND && seq != d->seq) {
        d->sent += length;
        d->seq = seq;
    } else if (op == 7) {
        d->total = address;
        d->state = PD_SEND;
    } else if (op == 8) {
        d->sent = address;
        d->state = d->total && address >= d->total ? PD_DONE : PD_PART;
    }
}

static int pd_restart(PdState *d)
{
    return d->state >= PD_DONE && ++d->beats >= PD_RESTART_BEATS;
}
