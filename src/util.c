/* String, memory, and number helpers without a C library. */
#include "os.h"

MemoryLayout memory;
u8 *const iobuf = (u8 *)MEM_IO_BASE;
char   name_scratch[64][64];
FatEnt fe_scratch[128];

int k_atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

const char *k_strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c) return s;
    return c == 0 ? s : 0;
}

const char *k_strstr(const char *hay, const char *needle)
{
    if (!*needle) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

int k_toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int k_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

void ksort(void *base, int n, int size, int (*cmp)(const void *, const void *))
{
    u8 tmp[256];
    u8 *a = base;
    if (size > (int)sizeof tmp) return;
    for (int i = 1; i < n; i++) {
        memcpy(tmp, a + i * size, size);
        int j = i - 1;
        while (j >= 0 && cmp(a + j * size, tmp) > 0) {
            memcpy(a + (j + 1) * size, a + j * size, size);
            j--;
        }
        memcpy(a + (j + 1) * size, tmp, size);
    }
}

static u32 rng_state;

void krand_seed(u32 seed) { rng_state = seed ? seed : 1; }

u32 krand(void)
{
    if (!rng_state) {
        u32 lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        rng_state = (lo ^ hi) | 1;
    }
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static char klog_buf[2048];
static int  klog_len;

void klog(const char *s)
{
    int n = (int)strlen(s);

    if (n > (int)sizeof klog_buf / 2 - 1) n = sizeof klog_buf / 2 - 1;
    if (klog_len + n + 1 > (int)sizeof klog_buf) {
        int keep = sizeof klog_buf / 2;
        memmove(klog_buf, klog_buf + klog_len - keep, keep);
        klog_len = keep;
    }
    memcpy(klog_buf + klog_len, s, n);
    klog_len += n;
    klog_buf[klog_len] = 0;
}

int klog_read(char *dst, int cap)
{
    int n = klog_len < cap - 1 ? klog_len : cap - 1;
    if (n < 0) return 0;
    memcpy(dst, klog_buf, n);
    dst[n] = 0;
    return n;
}

static char trace_buf[4096];
static int  trace_len;
static u32  trace_seq;

int trace_dirty;

u32 trace_seq_get(void) { return trace_seq; }
u32 trace_held(void)    { return (u32)trace_len; }

int trace_slice_read(u32 off, char *dst, u32 count)
{
    if (off >= (u32)trace_len) return 0;
    if (off + count > (u32)trace_len) count = (u32)trace_len - off;
    memcpy(dst, trace_buf + off, count);
    return (int)count;
}

void ktrace(const char *msg)
{
    char line[96];
    kfmt(line, sizeof line, "%us %s\n", ticks / 100, msg);
    int n = (int)strlen(line);
    if (n > (int)sizeof trace_buf / 2) n = sizeof trace_buf / 2;
    if (trace_len + n + 1 > (int)sizeof trace_buf) {
        int keep = sizeof trace_buf / 2;
        memmove(trace_buf, trace_buf + trace_len - keep, keep);
        trace_len = keep;
    }
    memcpy(trace_buf + trace_len, line, n);
    trace_len += n;
    trace_seq += (u32)n;
    trace_buf[trace_len] = 0;
    trace_dirty = 1;
}

int trace_read(char *dst, int cap)
{
    int n = trace_len < cap - 1 ? trace_len : cap - 1;
    if (n < 0) return 0;
    memcpy(dst, trace_buf, n);
    dst[n] = 0;
    return n;
}

int net_parse_ip(const char *s, u32 *out)
{
    u8 b[4];
    for (int i = 0; i < 4; i++) {
        int v = 0, any = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
        if (!any || v > 255) return 0;
        b[i] = v;
        if (i < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    *out = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return 1;
}

static int mmx_ok;

int  mmx_available(void) { return mmx_ok; }

void mmx_init(void)
{
    u32 r[4];
    cpuid_raw(0, r);
    if (r[0] == 0) return;
    cpuid_raw(1, r);
    mmx_ok = (r[3] & (1u << 23)) != 0;
    klog(mmx_ok ? "mmx: present - bulk copies accelerated\n"
                : "mmx: absent - scalar copies\n");
}

#define MMX_MIN 64

__attribute__((noinline))
void *memcpy(void *d, const void *s, u32 n)
{
    u8 *dd = d;
    const u8 *ss = s;

    if (mmx_ok && !in_irq && n >= MMX_MIN) {
        while (((u32)dd & 7) && n) { *dd++ = *ss++; n--; }
        u32 quads = n >> 3;
        if (quads) {

            __asm__ volatile(
                "movl %%ecx, %%eax\n\t"
                "shrl $3, %%eax\n\t"
                "jz 2f\n"
                "1:\n\t"
                "movq   (%1), %%mm0\n\t"  "movq  8(%1), %%mm1\n\t"
                "movq 16(%1), %%mm2\n\t"  "movq 24(%1), %%mm3\n\t"
                "movq 32(%1), %%mm4\n\t"  "movq 40(%1), %%mm5\n\t"
                "movq 48(%1), %%mm6\n\t"  "movq 56(%1), %%mm7\n\t"
                "movq %%mm0,   (%0)\n\t"  "movq %%mm1,  8(%0)\n\t"
                "movq %%mm2, 16(%0)\n\t"  "movq %%mm3, 24(%0)\n\t"
                "movq %%mm4, 32(%0)\n\t"  "movq %%mm5, 40(%0)\n\t"
                "movq %%mm6, 48(%0)\n\t"  "movq %%mm7, 56(%0)\n\t"
                "addl $64, %1\n\t"
                "addl $64, %0\n\t"
                "decl %%eax\n\t"
                "jnz 1b\n"
                "2:\n\t"
                "movl %%ecx, %%eax\n\t"
                "andl $7, %%eax\n\t"
                "jz 3f\n"
                "4:\n\t"
                "movq (%1), %%mm0\n\t"
                "movq %%mm0, (%0)\n\t"
                "addl $8, %1\n\t"
                "addl $8, %0\n\t"
                "decl %%eax\n\t"
                "jnz 4b\n"
                "3:\n\t"
                "emms\n\t"
                : "+r"(dd), "+r"(ss)
                : "c"(quads)
                : "eax", "memory");
            n &= 7;
        }
    }
    while (n >= 4) { *(u32 *)dd = *(const u32 *)ss; dd += 4; ss += 4; n -= 4; }
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, u32 n)
{
    u8 *dd = d;
    const u8 *ss = s;
    if (dd < ss) return memcpy(d, s, n);
    while (n--) dd[n] = ss[n];
    return d;
}

__attribute__((noinline))
void *memset(void *d, int c, u32 n)
{
    u8 *dd = d;
    u32 v = (u8)c;
    v |= v << 8; v |= v << 16;

    if (mmx_ok && !in_irq && n >= MMX_MIN) {
        while (((u32)dd & 7) && n) { *dd++ = (u8)c; n--; }
        u32 quads = n >> 3;
        if (quads) {

            __asm__ volatile(
                "movd %2, %%mm0\n\t"
                "punpckldq %%mm0, %%mm0\n\t"
                "movl %%ecx, %%eax\n\t"
                "shrl $3, %%eax\n\t"
                "jz 2f\n"
                "1:\n\t"
                "movq %%mm0,   (%0)\n\t"  "movq %%mm0,  8(%0)\n\t"
                "movq %%mm0, 16(%0)\n\t"  "movq %%mm0, 24(%0)\n\t"
                "movq %%mm0, 32(%0)\n\t"  "movq %%mm0, 40(%0)\n\t"
                "movq %%mm0, 48(%0)\n\t"  "movq %%mm0, 56(%0)\n\t"
                "addl $64, %0\n\t"
                "decl %%eax\n\t"
                "jnz 1b\n"
                "2:\n\t"
                "movl %%ecx, %%eax\n\t"
                "andl $7, %%eax\n\t"
                "jz 3f\n"
                "4:\n\t"
                "movq %%mm0, (%0)\n\t"
                "addl $8, %0\n\t"
                "decl %%eax\n\t"
                "jnz 4b\n"
                "3:\n\t"
                "emms\n\t"
                : "+r"(dd)
                : "c"(quads), "r"(v)
                : "eax", "memory");
            n &= 7;
        }
    }
    while (n >= 4) { *(u32 *)dd = v; dd += 4; n -= 4; }
    while (n--) *dd++ = (u8)c;
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, u32 n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}

u32 strlen(const char *s)
{
    u32 n = 0;
    while (*s++) n++;
    return n;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strcasecmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return (u8)lower(*a) - (u8)lower(*b);
}

void strlcpy(char *d, const char *s, int cap)
{
    int i = 0;
    while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void one_dp(u32 whole, u32 rem, u32 div, const char *unit, char *b, int cap)
{
    u32 tenths = (rem * 10 + div / 2) / div;
    if (tenths >= 10) { whole++; tenths = 0; }
    kfmt(b, cap, "%u.%u %s", whole, tenths, unit);
}

void human_size(u32 bytes, char *buf, int cap)
{
    if (bytes < 1024) { kfmt(buf, cap, "%u B", bytes); return; }
    if (bytes < 1024u * 1024) {
        if (bytes < 10u * 1024) one_dp(bytes / 1024, bytes % 1024, 1024, "KB", buf, cap);
        else kfmt(buf, cap, "%u KB", (bytes + 512) / 1024);
        return;
    }
    if (bytes < 1024u * 1024 * 1024) {
        u32 mb = bytes / (1024u * 1024);
        one_dp(mb, bytes % (1024u * 1024), 1024u * 1024, "MB", buf, cap);
        return;
    }
    u32 gb = bytes / (1024u * 1024 * 1024);
    one_dp(gb, bytes % (1024u * 1024 * 1024), 1024u * 1024 * 1024, "GB", buf, cap);
}

void human_size_kb(u32 kb, char *buf, int cap)
{
    if (kb < 1024) { kfmt(buf, cap, "%u KB", kb); return; }
    if (kb < 1024u * 1024) {
        one_dp(kb / 1024, kb % 1024, 1024, "MB", buf, cap);
        return;
    }
    one_dp(kb / (1024u * 1024), kb % (1024u * 1024), 1024u * 1024, "GB", buf, cap);
}

static int numstr(char *out, u32 v, u32 base, int neg)
{
    char rb[12];
    int n = 0, m = 0;
    do {
        u32 d = v % base;
        rb[n++] = d < 10 ? '0' + d : 'a' + d - 10;
        v /= base;
    } while (v);
    if (neg) out[m++] = '-';
    while (n) out[m++] = rb[--n];
    out[m] = 0;
    return m;
}

__attribute__((minsize)) void kfmt(char *dst, int cap, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int o = 0;

#define PUT(ch) do { char c_ = (ch); if (o < cap - 1) dst[o++] = c_; } while (0)
    while (*f) {
        if (*f != '%') { PUT(*f); f++; continue; }
        f++;
        char padc = ' ';
        int pad = 0, left = 0;

        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { padc = '0'; f++; }
        while (*f >= '0' && *f <= '9') pad = pad * 10 + (*f++ - '0');

        char tmp[16];
        const char *s = tmp;
        int len = 0;
        char cc;
        switch (*f) {
        case 's': s = va_arg(ap, const char *); if (!s) s = "(null)"; len = strlen(s); break;
        case 'c': cc = (char)va_arg(ap, int); tmp[0] = cc; tmp[1] = 0; len = 1; break;
        case 'd': { int v = va_arg(ap, int); len = numstr(tmp, v < 0 ? (u32)-v : (u32)v, 10, v < 0); break; }
        case 'u': len = numstr(tmp, va_arg(ap, u32), 10, 0); break;
        case 'x': len = numstr(tmp, va_arg(ap, u32), 16, 0); break;
        case '%': tmp[0] = '%'; tmp[1] = 0; len = 1; break;
        default:  tmp[0] = *f; tmp[1] = 0; len = 1; break;
        }
        if (left) {
            while (*s) { PUT(*s++); }
            while (len < pad--) PUT(' ');
        } else {
            while (len < pad--) PUT(padc);
            while (*s) PUT(*s++);
        }
        f++;
    }
    dst[o] = 0;
#undef PUT
    va_end(ap);
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_encode(const u8 *in, u32 n, char *out, int cap)
{
    int o = 0;
    for (u32 i = 0; i < n; i += 3) {
        u32 b0 = in[i];
        u32 b1 = i + 1 < n ? in[i + 1] : 0;
        u32 b2 = i + 2 < n ? in[i + 2] : 0;
        u32 v = (b0 << 16) | (b1 << 8) | b2;
        if (o + 4 >= cap) return -1;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int b64_decode(const char *in, u8 *out, int cap)
{
    int o = 0, bits = 0;
    u32 acc = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r' || *p == ' ') continue;
        int v = b64val(*p);
        if (v < 0) return -1;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (u8)(acc >> bits);
        }
    }
    return o;
}

u32 crc32(const void *data, u32 n)
{
    const u8 *p = data;
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (u32)(-(i32)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

u32 hash_fnv(const void *data, u32 n)
{
    const u8 *p = data;
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static char hexdig(int v) { return v < 10 ? '0' + v : 'a' + v - 10; }

void uuid_gen(char *out)
{
    u8 b[16];
    for (int i = 0; i < 16; i += 4) {
        u32 r = krand();
        b[i] = r; b[i + 1] = r >> 8; b[i + 2] = r >> 16; b[i + 3] = r >> 24;
    }
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    int o = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hexdig(b[i] >> 4);
        out[o++] = hexdig(b[i] & 15);
    }
    out[o] = 0;
}

const char *path_base(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == ':') b = q + 1;
    return b;
}

const char *path_ext(const char *p)
{
    const char *b = path_base(p), *dot = 0;
    for (const char *q = b; *q; q++)
        if (*q == '.') dot = q;
    return (dot && dot[1]) ? dot + 1 : "";
}

void path_dir(const char *p, char *out, int cap)
{
    int cut = -1;
    for (int i = 0; p[i]; i++)
        if (p[i] == '/' || p[i] == ':') cut = i;
    if (cut < 0) { if (cap) out[0] = 0; return; }
    int n = p[cut] == ':' ? cut + 1 : cut;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

void path_join(char *out, int cap, const char *dir, const char *name)
{
    int n = strlen(dir);
    int trail = n && (dir[n - 1] == '/' || dir[n - 1] == ':');
    kfmt(out, cap, "%s%s%s", dir, (n && !trail) ? "/" : "", name);
}

static const char *const MON3[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
static const char *const MONF[12] = {
    "January","February","March","April","May","June","July","August",
    "September","October","November","December" };
static const char *const WD3[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

static int dow(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void date_fmt(u32 dt, const char *fmt, char *out, int cap)
{
    int Y = 1980 + ((dt >> 25) & 0x7F);
    int M = (dt >> 21) & 0x0F;
    int D = (dt >> 16) & 0x1F;
    int h = (dt >> 11) & 0x1F;
    int mi = (dt >> 5) & 0x3F;
    int s = (dt & 0x1F) * 2;
    if (M < 1) M = 1; if (M > 12) M = 12;
    int o = 0;
    char t[16];
    for (const char *p = fmt; *p && o < cap - 1; p++) {
        if (*p != '%') { out[o++] = *p; continue; }
        p++;
        t[0] = 0;
        switch (*p) {
        case 'Y': kfmt(t, sizeof t, "%04d", Y); break;
        case 'y': kfmt(t, sizeof t, "%02d", Y % 100); break;
        case 'm': kfmt(t, sizeof t, "%02d", M); break;
        case 'd': kfmt(t, sizeof t, "%02d", D); break;
        case 'H': kfmt(t, sizeof t, "%02d", h); break;
        case 'M': kfmt(t, sizeof t, "%02d", mi); break;
        case 'S': kfmt(t, sizeof t, "%02d", s); break;
        case 'b': strlcpy(t, MON3[M - 1], sizeof t); break;
        case 'B': strlcpy(t, MONF[M - 1], sizeof t); break;
        case 'a': strlcpy(t, WD3[dow(Y, M, D)], sizeof t); break;
        case 'p': strlcpy(t, h < 12 ? "AM" : "PM", sizeof t); break;
        case 'I': kfmt(t, sizeof t, "%02d", h % 12 ? h % 12 : 12); break;
        case '%': t[0] = '%'; t[1] = 0; break;
        default:  out[o++] = '%'; if (*p) t[0] = 0, out[o < cap - 1 ? o++ : o] = *p; break;
        }
        for (int i = 0; t[i] && o < cap - 1; i++) out[o++] = t[i];
        if (!*p) break;
    }
    out[o] = 0;
}

void ms_open(MemStream *s, u8 *buf, u32 cap)
{
    s->buf = buf; s->cap = cap; s->len = 0; s->pos = 0; s->owned = 0;
}

int ms_alloc(MemStream *s, u32 cap)
{
    if (!cap) cap = 256;
    s->buf = kmalloc(cap);
    if (!s->buf) { s->cap = s->len = s->pos = 0; s->owned = 0; return 0; }
    s->cap = cap; s->len = 0; s->pos = 0; s->owned = 1;
    return 1;
}

void ms_free(MemStream *s)
{
    if (s->owned && s->buf) kfree(s->buf);
    s->buf = 0; s->cap = s->len = s->pos = 0; s->owned = 0;
}

#define MS_MAX (1u << 30)

static int ms_grow(MemStream *s, u32 need)
{

    if (need > MS_MAX || s->pos > MS_MAX - need) return 0;
    u32 want = s->pos + need;
    if (want <= s->cap) return 1;
    if (!s->owned) return 0;
    u32 nc = s->cap ? s->cap : 256;
    while (nc < want) nc *= 2;
    u8 *nb = kmalloc(nc);
    if (!nb) return 0;
    if (s->len) memcpy(nb, s->buf, s->len);
    kfree(s->buf);
    s->buf = nb; s->cap = nc;
    return 1;
}

int ms_write(MemStream *s, const void *data, u32 n)
{
    if (!ms_grow(s, n)) {
        u32 room = s->pos < s->cap ? s->cap - s->pos : 0;
        n = n < room ? n : room;
        if (!n) return 0;
    }
    memcpy(s->buf + s->pos, data, n);
    s->pos += n;
    if (s->pos > s->len) s->len = s->pos;
    return (int)n;
}

int ms_read(MemStream *s, void *out, u32 n)
{
    u32 avail = s->pos < s->len ? s->len - s->pos : 0;
    if (n > avail) n = avail;
    if (n) memcpy(out, s->buf + s->pos, n);
    s->pos += n;
    return (int)n;
}

int ms_seek(MemStream *s, u32 pos)
{
    if (pos > s->len) return -1;
    s->pos = pos;
    return 0;
}
