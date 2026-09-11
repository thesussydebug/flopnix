/* Reads tracks and events from MIDI files. */
#pragma once

#define MF_MAXTRK 24

static inline int mf_datalen(u8 status)
{
    switch (status & 0xF0) {
    case 0xC0: case 0xD0:
        return 1;
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
        return 2;
    }
    return 0;
}

static inline u32 mf_vlq(const u8 *p, u32 n, u32 *i)
{
    if (!p || !i || *i >= n) return 0;
    u32 v = 0;
    for (int k = 0; k < 4 && *i < n; k++) {
        u8 b = p[(*i)++];
        v = (v << 7) | (u32)(b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

static inline int mf_header(const u8 *p, u32 n, int *ntrk, u32 *first)
{
    if (!p || n < 14) return 0;
    if (p[0] != 'M' || p[1] != 'T' || p[2] != 'h' || p[3] != 'd') return 0;
    u32 len = ((u32)p[4] << 24) | ((u32)p[5] << 16) | ((u32)p[6] << 8) | p[7];
    if (len < 6) return 0;
    int fmt = (p[8] << 8) | p[9];
    int tr  = (p[10] << 8) | p[11];
    int div = (p[12] << 8) | p[13];
    if (fmt != 0 && fmt != 1) return 0;
    if (p[12] & 0x80) return 0;
    if (div <= 0 || tr < 1) return 0;
    u32 f = 8 + len;
    if (f + 8 > n) return 0;
    if (ntrk)  *ntrk  = tr > MF_MAXTRK ? MF_MAXTRK : tr;
    if (first) *first = f;
    return div;
}

static inline u32 mf_tickrate(u32 us_per_qn, int division)
{
    if (!us_per_qn || division <= 0) return 0;
    return us_per_qn / ((u32)division * 10);
}

typedef struct {
    const u8 *p;
    u32 n;
    u32 i;
    u32 tick;
    u8  running;
    u8  ended;
} MfTrack;

#define MF_META 0xFF

static inline int mf_next(MfTrack *t, u8 *status, u8 *d1, u8 *d2)
{
    if (!t || !t->p || t->ended || t->i >= t->n) return 0;
    t->tick += mf_vlq(t->p, t->n, &t->i);
    if (t->i >= t->n) return 0;

    u8 s = t->p[t->i];
    if (s & 0x80) {
        t->i++;
        if (s < 0xF0) t->running = s;
    } else {
        s = t->running;
        if (!s) return 0;
    }

    if (s == 0xFF) {
        if (t->i >= t->n) return 0;
        u8 type = t->p[t->i++];
        u32 len = mf_vlq(t->p, t->n, &t->i);
        if (type == 0x2F) t->ended = 1;
        if (status) *status = MF_META;
        if (d1) *d1 = type;
        if (d2) *d2 = 0;

        t->i += len;
        return 1;
    }
    if (s == 0xF0 || s == 0xF7) {
        u32 len = mf_vlq(t->p, t->n, &t->i);
        t->i += len;
        if (status) *status = MF_META;
        if (d1) *d1 = 0;
        if (d2) *d2 = 0;
        return 1;
    }

    int nd = mf_datalen(s);
    if (nd < 1 || t->i + (u32)nd > t->n) return 0;
    if (status) *status = s;
    if (d1) *d1 = t->p[t->i];
    if (d2) *d2 = nd > 1 ? t->p[t->i + 1] : 0;
    t->i += (u32)nd;
    return 1;
}

static inline u32 mf_note_hz(int note)
{
    static const u32 T[12] = { 26163, 27718, 29366, 31113, 32963, 34923,
                               36999, 39200, 41530, 44000, 46616, 49388 };
    if (note < 0 || note > 127) return 0;
    int oct = note / 12, semi = note % 12;
    if (oct >= 5) return (T[semi] << (oct - 5)) / 100;
    return T[semi] / (100u << (5 - oct));
}
