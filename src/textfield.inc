/* Edits a line of text with a cursor and selection. */
static inline int kb_unctrl(int k, int ctrl)
{
    return (ctrl && k >= 1 && k <= 26) ? k - 1 + 'a' : k;
}

typedef struct {
    char *buf;
    int   cap;
    int   len;
    int   caret;
    int   all;
} TextField;

static inline void tf_clear_all(TextField *t)
{
    t->len = 0;
    t->caret = 0;
    t->all = 0;
    t->buf[0] = 0;
}

static inline int tf_key(TextField *t, int k, int printable)
{
    switch (k) {
    case K_LEFT:
        if (t->all) { t->all = 0; t->caret = 0; }
        else if (t->caret > 0) t->caret--;
        return 1;
    case K_RIGHT:
        if (t->all) { t->all = 0; t->caret = t->len; }
        else if (t->caret < t->len) t->caret++;
        return 1;
    case K_HOME: t->all = 0; t->caret = 0;       return 1;
    case K_END:  t->all = 0; t->caret = t->len;  return 1;
    case '\b':
        if (t->all) { tf_clear_all(t); return 1; }
        if (t->caret > 0) {
            for (int i = t->caret - 1; i < t->len; i++) t->buf[i] = t->buf[i + 1];
            t->caret--;
            t->len--;
        }
        return 1;
    case K_DEL:
        if (t->all) { tf_clear_all(t); return 1; }
        if (t->caret < t->len) {
            for (int i = t->caret; i < t->len; i++) t->buf[i] = t->buf[i + 1];
            t->len--;
        }
        return 1;
    default: break;
    }
    if (!printable) return 0;
    if (t->all) tf_clear_all(t);
    if (t->len >= t->cap - 1) return 1;
    for (int i = t->len; i > t->caret; i--) t->buf[i] = t->buf[i - 1];
    t->buf[t->caret++] = (char)k;
    t->len++;
    t->buf[t->len] = 0;
    return 1;
}
