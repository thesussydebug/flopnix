/* Edits sticky-note text in a fixed buffer. */
#pragma once

typedef struct {
    char *buf;
    int   cap;
    int   len;
    int   car;
} NoteBuf;

static inline int nb_line_end(const char *s, int len, int from, int cols)
{
    if (!s || from >= len) return len < 0 ? 0 : len;
    if (cols < 1) cols = 1;
    int e = from, lastsp = -1;
    while (e < len && e - from < cols) {
        if (s[e] == '\n') return e;
        if (s[e] == ' ') lastsp = e;
        e++;
    }
    if (e >= len) return len;
    if (s[e] == '\n') return e;
    if (s[e] == ' ') return e;
    if (lastsp > from) return lastsp;
    return e;
}

static inline int nb_next_line(const char *s, int len, int from, int cols)
{
    if (!s || from >= len) return len < 0 ? 0 : len;
    int e = nb_line_end(s, len, from, cols);
    if (e < len && s[e] == '\n') return e + 1;
    int n = e;
    while (n < len && s[n] == ' ') n++;
    if (n <= from) n = from + 1;
    return n;
}

static inline int nb_insert(NoteBuf *n, int ch)
{
    if (!n || !n->buf || n->len + 1 >= n->cap) return 0;
    if (n->car < 0) n->car = 0;
    if (n->car > n->len) n->car = n->len;
    for (int i = n->len; i > n->car; i--) n->buf[i] = n->buf[i - 1];
    n->buf[n->car] = (char)ch;
    n->len++;
    n->car++;
    n->buf[n->len] = 0;
    return 1;
}

static inline int nb_backspace(NoteBuf *n)
{
    if (!n || !n->buf || n->car <= 0) return 0;
    for (int i = n->car - 1; i < n->len - 1; i++) n->buf[i] = n->buf[i + 1];
    n->len--;
    n->car--;
    n->buf[n->len] = 0;
    return 1;
}

static inline int nb_delete(NoteBuf *n)
{
    if (!n || !n->buf || n->car >= n->len) return 0;
    for (int i = n->car; i < n->len - 1; i++) n->buf[i] = n->buf[i + 1];
    n->len--;
    n->buf[n->len] = 0;
    return 1;
}
