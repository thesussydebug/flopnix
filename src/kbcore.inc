/* Converts keyboard scan codes to keys and modifier state. */
static const char kb_map[64] = {
      0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
   '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
      0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
      0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
      0, ' ', 0, 0, 0, 0, 0, 0
};
static const char kb_map_sh[64] = {
      0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
   '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
      0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
      0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
      0, ' ', 0, 0, 0, 0, 0, 0
};

static const char kb_kp_num[13] = {
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};
static const u16 kb_kp_nav[13] = {
    K_HOME, K_UP, K_PGUP, '-', K_LEFT, 0, K_RIGHT, '+',
    K_END, K_DOWN, K_PGDN, 0, K_DEL
};

#ifndef K_MENU
#define K_MENU  0x200
#endif
#ifndef K_PRTSC
#define K_PRTSC 0x201
#endif

typedef struct {
    u8 e0;
    u8 shift_l, shift_r, ctrl, caps;
    u8 numlock;

    u8 alt_l, alt_r;
} KbSt;

#define KB_NUMLOCK_DEFAULT 1

static inline int kb_feed(KbSt *st, u8 sc, int *track, int *down)
{
    *track = 0;
    *down = 0;
    if (sc == 0xE0) { st->e0 = 1; return 0; }
    int rel = sc & 0x80;
    u8 code = sc & 0x7F;

    if (st->e0) {
        st->e0 = 0;
        if (code == 0x1D) { st->ctrl = (u8)!rel; return 0; }
        if (code == 0x38) { st->alt_r = (u8)!rel; return 0; }
        int kv = 0;
        switch (code) {
        case 0x48: kv = K_UP; break;
        case 0x50: kv = K_DOWN; break;
        case 0x4B: kv = K_LEFT; break;
        case 0x4D: kv = K_RIGHT; break;
        case 0x47: kv = K_HOME; break;
        case 0x4F: kv = K_END; break;
        case 0x53: kv = K_DEL; break;
        case 0x49: kv = K_PGUP; break;
        case 0x51: kv = K_PGDN; break;
        case 0x1C: kv = '\n'; break;
        case 0x35: kv = '/';  break;
        case 0x5B:
        case 0x5C:
        case 0x5D: kv = K_MENU; break;
        case 0x37: kv = K_PRTSC; break;

        }
        if (!kv) return 0;
        *track = kv;
        *down = !rel;
        return rel ? 0 : kv;
    }

    if (code == 0x2A) { st->shift_l = (u8)!rel; return 0; }
    if (code == 0x36) { st->shift_r = (u8)!rel; return 0; }
    if (code == 0x1D) { st->ctrl = (u8)!rel; return 0; }
    if (code == 0x38) { st->alt_l = (u8)!rel; return 0; }
    if (code == 0x3A) { if (!rel) st->caps ^= 1; return 0; }
    if (code == 0x45) { if (!rel) st->numlock ^= 1; return 0; }

    if (code >= 0x47 && code <= 0x53) {
        int kv = st->numlock ? (int)kb_kp_num[code - 0x47]
                             : (int)kb_kp_nav[code - 0x47];
        if (!kv) return 0;
        *track = kv;
        *down = !rel;
        return rel ? 0 : kv;
    }

    if (code >= 64) return 0;
    *track = kb_map[code];
    *down = !rel;
    if (rel) return 0;
    int sh = st->shift_l || st->shift_r;
    char ch = sh ? kb_map_sh[code] : kb_map[code];
    if (st->caps && ch >= 'a' && ch <= 'z') ch += 'A' - 'a';
    else if (st->caps && sh && ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (st->ctrl && ch >= 'a' && ch <= 'z') return ch - 'a' + 1;
    if (st->ctrl && ch >= 'A' && ch <= 'Z') return ch - 'A' + 1;
    return ch;
}
