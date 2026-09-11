/* Reads and writes the saved Notes index. */
#pragma once

#define NL_NAMEMAX 24
#define NL_MINW 120
#define NL_MINH 70
#define NL_MAXW 620
#define NL_MAXH 440

typedef struct {
    int  x, y, w, h;
    int  open;
    char file[NL_NAMEMAX];
} NoteRec;

static inline int nl_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int nl_field(const char **p, int *out)
{
    const char *s = *p;
    int neg = 0, got = 0, v = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; got = 1; }
    if (!got || *s != '\t') return 0;
    *out = neg ? -v : v;
    *p = s + 1;
    return 1;
}

static inline int nl_parse(const char *line, NoteRec *out)
{
    if (!line || !out) return 0;
    const char *p = line;
    int x, y, w, h, open;
    if (!nl_field(&p, &x) || !nl_field(&p, &y) || !nl_field(&p, &w) ||
        !nl_field(&p, &h) || !nl_field(&p, &open))
        return 0;
    if (!*p) return 0;

    out->x = nl_clamp(x, 0, NL_MAXW);
    out->y = nl_clamp(y, 0, NL_MAXH);
    out->w = nl_clamp(w, NL_MINW, NL_MAXW);
    out->h = nl_clamp(h, NL_MINH, NL_MAXH);
    out->open = open ? 1 : 0;
    int i = 0;
    while (p[i] && i < NL_NAMEMAX - 1) { out->file[i] = p[i]; i++; }
    out->file[i] = 0;
    return 1;
}

static inline int nl_num(char *d, int cap, int v)
{
    char t[12];
    int n = 0;
    if (v < 0) v = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < (int)sizeof t);
    if (n >= cap) return 0;
    for (int i = 0; i < n; i++) d[i] = t[n - 1 - i];
    return n;
}

static inline int nl_fmt(char *dst, int cap, const NoteRec *r)
{
    if (!dst || !r || cap <= 0) return 0;
    int vals[5] = { r->x, r->y, r->w, r->h, r->open };
    int o = 0;
    for (int f = 0; f < 5; f++) {
        int n = nl_num(dst + o, cap - o - 1, vals[f]);
        if (!n) return 0;
        o += n;
        if (o + 1 >= cap) return 0;
        dst[o++] = '\t';
    }
    for (int i = 0; r->file[i]; i++) {
        if (o + 1 >= cap) return 0;
        dst[o++] = r->file[i];
    }
    if (o >= cap) return 0;
    dst[o] = 0;
    return o;
}
