/* Assembles VFAT long file names. */
#pragma once

typedef struct {
    char name[64];
    int  top;
    int  seen;
    u8   csum;
    u8   ok;
} LfnAcc;

static inline u8 lfn_checksum(const u8 *sfn)
{
    u8 s = 0;
    for (int i = 0; i < 11; i++)
        s = (u8)(((s & 1) << 7) + (s >> 1) + sfn[i]);
    return s;
}

static inline void lfn_reset(LfnAcc *a)
{

    for (int i=0;i<(int)sizeof a->name;i++) a->name[i]=0;
    a->top = a->seen = 0;
    a->csum = 0;
    a->ok = 0;
}

static inline void lfn_feed(LfnAcc *a, const u8 *de)
{
    int ord = de[0] & 0x3F;

    if (ord < 1 || ord > 20) { a->ok=0; return; }
    if (de[0] & 0x40) {
        lfn_reset(a);
        if (ord < 1 || ord > 20) return;
        a->top = ord;
        a->csum = de[13];
        a->ok = 1;
    } else {

        if (!a->ok || ord != a->top - a->seen || de[13] != a->csum) {
            a->ok = 0;
            return;
        }
    }

    static const u8 slot[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    for (int c = 0; c < 13; c++) {
        int pos = (ord - 1) * 13 + c;
        if (pos >= (int)sizeof a->name - 1) continue;
        u16 w = (u16)(de[slot[c]] | (de[slot[c] + 1] << 8));
        if (w == 0xFFFF) continue;
        if (w == 0)      { a->name[pos] = 0; continue; }
        a->name[pos] = (w >= 32 && w <= 126) ? (char)w : '?';
    }
    a->seen++;
}

static inline int lfn_take(LfnAcc *a, const u8 *de83, char *out, int cap)
{
    int good = a->ok && a->top > 0 && a->seen == a->top &&
               lfn_checksum(de83) == a->csum;
    if (good) {
        a->name[sizeof a->name - 1] = 0;
        int i = 0;
        for (; a->name[i] && i < cap - 1; i++) out[i] = a->name[i];
        out[i] = 0;
        good = out[0] != 0;
    }
    lfn_reset(a);
    return good;
}
