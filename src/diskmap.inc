/* Turns floppy allocation data into a text map. */
#pragma once

static inline char dm_char(int cls)
{
    switch (cls) {
    case FH_OK:    return '=';
    case FH_SLOW:  return '~';
    case FH_RETRY: return '?';
    case FH_BAD:   return 'X';
    default:       return '.';
    }
}

static inline void dm_ruler(int start, int n, char *out, int cap)
{
    int i = 0;
    for (; i < n && i < cap - 1; i++) {
        int c = start + i;
        out[i] = (c % 10 == 0) ? (char)('0' + (c / 10) % 10)
               : (c % 5 == 0)  ? ':' : '.';
    }
    out[i] = 0;
}

static inline void dm_row(const unsigned char *cells, int start, int n,
                          char *out, int cap)
{
    int i = 0;
    for (; i < n && i < cap - 1; i++) out[i] = dm_char(cells[start + i]);
    out[i] = 0;
}

static inline void dm_tally(unsigned ok, unsigned slow, unsigned retry,
                            unsigned bad, char *out, int cap)
{
    static const char *const names[4] = { " ok, ", " slow, ", " retried, ",
                                          " bad" };
    unsigned v[4] = { ok, slow, retry, bad };
    int p = 0;
    for (int i = 0; i < 4 && p < cap - 1; i++) {
        unsigned x = v[i];
        char d[12];
        int nd = 0;
        do { d[nd++] = (char)('0' + x % 10); x /= 10; } while (x);
        while (nd && p < cap - 1) out[p++] = d[--nd];
        for (const char *s = names[i]; *s && p < cap - 1; s++) out[p++] = *s;
    }
    out[p] = 0;
}
