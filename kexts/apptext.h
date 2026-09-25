#include "textedit.inc"

static int at_replace(TextEdit *t, const char *s, int n, int (*grow)(TextEdit *, int))
{
    int a=te_start(t),b=te_end(t);
    if(n<0||a<0||b>t->len)return -1;
    int kept=t->len-(b-a);
    if(n>0x7FFFFFFF-kept)return -1;
    int need=kept+n;
    if (need >= t->cap && (!grow || !grow(t, need))) return -1;
    return te_replace(t, s, n);
}

static int at_key(TextEdit *t, int k, int multiline, int readonly, int (*grow)(TextEdit *, int))
{
    if (te_nav(t, k, api->kbd_mods())) return 1;
    if (k == 3 || k == 24) {
        int a = te_start(t), n = te_end(t) - a;
        if (!n || (k == 24 && readonly)) return 1;
        char clip[4096];
        if (n >= (int)sizeof clip) return -1;
        for (int i = 0; i < n; i++) clip[i] = t->buf[a + i];
        clip[n] = 0;
        if (api->clip_set_text(clip) < 0) return -1;
        return k == 24 ? te_replace(t, "", 0) : 1;
    }
    if (readonly) return 1;
    if (k == 22) {
        char clip[4096];
        if (api->clip_get_text(clip, sizeof clip) <= 0) return 1;
        int n = te_clean(clip, multiline);
        return n ? at_replace(t, clip, n, grow) : 1;
    }
    if (k == '\b' || k == K_DEL) {
        int anchor = t->anchor;
        if (anchor == t->caret) {
            if (k == '\b' && t->caret > 0) t->anchor--;
            if (k == K_DEL && t->caret < t->len) t->anchor++;
        }
        int r = te_replace(t, "", 0);
        if (r < 0) t->anchor = anchor;
        return r;
    }
    if (multiline && (k == '\n' || k == '\r')) return at_replace(t, "\n", 1, grow);
    if (multiline && k == '\t') return at_replace(t, "    ", 4, grow);
    if (k >= 32 && k < 127) { char c = (char)k; return at_replace(t, &c, 1, grow); }
    return 0;
}
