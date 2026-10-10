typedef struct { u8 state, verb, cr, echo, ours, theirs, sbn, sb[8]; } TnClient;

static inline void tn_add(u8 *out, int *used, int cap, const u8 *src, int n)
{
    if (*used + n > cap) return;
    for (int i = 0; i < n; i++) out[(*used)++] = src[i];
}

static inline u8 tn_ours(u8 option)
{
    return option == 24 ? 1 : option == 31 ? 2 : option == 3 ? 4 : 0;
}

static inline u8 tn_theirs(u8 option)
{
    return option == 1 ? 1 : option == 3 ? 2 : 0;
}

static inline int tn_byte(TnClient *t, u8 c, u8 *out, int *used, int cap, int cols, int rows)
{
    u8 r[3] = { 255, 0, c };
    switch (t->state) {
    case 1:
        t->state = 0;
        if (c == 255) return 255;
        if (c >= 251 && c <= 254) { t->verb = c; t->state = 2; }
        else if (c == 250) { t->state = 3; t->sbn = 0; }
        return -1;
    case 2:
        t->state = 0;
        if (t->verb == 253) {
            u8 bit = tn_ours(c);
            if (bit && (t->ours & bit)) return -1;
            t->ours |= bit;
            r[1] = bit ? 251 : 252;
            tn_add(out, used, cap, r, 3);
            if (c == 31) {
                u8 size[9] = { 255, 250, 31, 0, (u8)cols, 0, (u8)rows, 255, 240 };
                tn_add(out, used, cap, size, 9);
            }
        } else if (t->verb == 254) {
            u8 bit = tn_ours(c);
            if (!(t->ours & bit)) return -1;
            t->ours &= (u8)~bit;
            r[1] = 252;
            tn_add(out, used, cap, r, 3);
        } else if (t->verb == 251) {
            u8 bit = tn_theirs(c);
            if (bit && (t->theirs & bit)) return -1;
            t->theirs |= bit;
            if (c == 1) t->echo = 1;
            r[1] = bit ? 253 : 254;
            tn_add(out, used, cap, r, 3);
        } else {
            u8 bit = tn_theirs(c);
            if (!(t->theirs & bit)) return -1;
            t->theirs &= (u8)~bit;
            if (c == 1) t->echo = 0;
            r[1] = 254;
            tn_add(out, used, cap, r, 3);
        }
        return -1;
    case 3:
        if (c == 255) t->state = 4;
        else if (t->sbn < sizeof t->sb) t->sb[t->sbn++] = c;
        return -1;
    case 4:
        if (c != 240) { t->state = 3; return -1; }
        t->state = 0;
        if (t->sbn >= 2 && t->sb[0] == 24 && t->sb[1] == 1) {
            static const u8 kind[10] = { 255, 250, 24, 0, 'A', 'N', 'S', 'I', 255, 240 };
            tn_add(out, used, cap, kind, 10);
        }
        return -1;
    }
    if (c == 255) { t->state = 1; return -1; }
    if (t->cr) { t->cr = 0; if (c == 0 || c == 10) return -1; }
    if (c == 13) { t->cr = 1; return 10; }
    if (c == 9) return ' ';
    if (c < 32 && c != 10 && c != 8 && c != 27) return -1;
    return c;
}

static inline int tn_key(int k, u8 *out)
{
    int arrow = k == K_UP ? 'A' : k == K_DOWN ? 'B' : k == K_RIGHT ? 'C' : k == K_LEFT ? 'D' : 0;
    if (arrow) { out[0] = 27; out[1] = '['; out[2] = (u8)arrow; return 3; }
    if (k <= 0 || k > 255) return 0;
    if (k == '\n') { out[0] = 13; out[1] = 10; return 2; }
    if (k == '\b') { out[0] = 127; return 1; }
    out[0] = (u8)k;
    if (k == 255) { out[1] = 255; return 2; }
    return 1;
}
