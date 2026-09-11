/* Reads commands from AUTOEXEC one line at a time. */
#pragma once

#define AX_LINE 128

static inline int ax_next(const char *buf, int len, int *pos, char *out, int cap)
{
    while (*pos < len) {
        int o = 0;
        while (*pos < len && buf[*pos] != '\n') {
            char c = buf[(*pos)++];
            if (c == '\r') continue;
            if (o < cap - 1) out[o++] = c;
        }
        if (*pos < len) (*pos)++;
        out[o] = 0;
        int blank = 1;
        for (int i = 0; i < o; i++)
            if (out[i] != ' ' && out[i] != '\t') { blank = 0; break; }
        if (blank || out[0] == '#') continue;
        return 1;
    }
    return 0;
}
