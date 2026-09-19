#pragma once

typedef struct {
    char *buf;
    int cap, len, caret, anchor;
} TextEdit;

static inline int te_start(const TextEdit *t) { return t->caret < t->anchor ? t->caret : t->anchor; }
static inline int te_end(const TextEdit *t) { return t->caret > t->anchor ? t->caret : t->anchor; }
static inline int te_selected(const TextEdit *t, int i) { return i >= te_start(t) && i < te_end(t); }

static inline void te_move(TextEdit *t, int pos, int extend)
{
    if (pos < 0) pos = 0;
    if (pos > t->len) pos = t->len;
    t->caret = pos;
    if (!extend) t->anchor = pos;
}

static inline int te_replace(TextEdit *t, const char *s, int n)
{
    int a = te_start(t), b = te_end(t), len = t->len - (b - a) + n;
    if (!t->buf || n < 0 || len >= t->cap) return -1;
    if (n == 0 && a == b) return 1;
    if (n > b - a) {
        for (int i = t->len; i >= b; i--) t->buf[i + n - (b - a)] = t->buf[i];
    } else {
        for (int i = b; i <= t->len; i++) t->buf[i + n - (b - a)] = t->buf[i];
    }
    for (int i = 0; i < n; i++) t->buf[a + i] = s[i];
    t->len = len;
    t->caret = t->anchor = a + n;
    return 2;
}

static inline int te_clean(char *s, int multiline)
{
    int n = 0;
    for (int i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n') {
            if (!multiline) break;
            if (c == '\r' && s[i + 1] == '\n') i++;
            s[n++] = '\n';
        } else if (c == '\t') s[n++] = ' ';
        else if (c >= 32 && c != 127) s[n++] = (char)c;
    }
    s[n] = 0;
    return n;
}

static inline int te_nav(TextEdit *t, int k, int mods)
{
    int p = t->caret, shift = mods & 1, ctrl = mods & 2;
    if (k == 1) { t->anchor = 0; t->caret = t->len; return 1; }
    if (k == K_LEFT) p = !shift && t->anchor != p ? te_start(t) : p - 1;
    else if (k == K_RIGHT) p = !shift && t->anchor != p ? te_end(t) : p + 1;
    else if (k == K_HOME) { if (ctrl) p = 0; else while (p > 0 && t->buf[p - 1] != '\n') p--; }
    else if (k == K_END) { if (ctrl) p = t->len; else while (p < t->len && t->buf[p] != '\n') p++; }
    else return 0;
    te_move(t, p, shift);
    return 1;
}
