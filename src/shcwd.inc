/* Resolves a path against the shell's current folder. */
#pragma once

#define SC_MAX 96

static inline int sc_resolve(const char *cwd, const char *arg, char *out, int cap)
{
    if (!out || cap < 1) return 0;
    if (!arg) arg = "";
    if (!cwd) cwd = "";

    const char *a = arg;
    int absolute = 0;
    for (const char *p = a; *p && *p != '/' && *p != '\\'; p++)
        if (*p == ':') { a = p + 1; absolute = 1; break; }
    if (*a == '/' || *a == '\\') absolute = 1;

    char buf[128];
    int n = 0;
    if (!absolute)
        for (const char *p = cwd; *p; p++) {if(n>=(int)sizeof buf-1){out[0]=0;return 0;}buf[n++] = *p;}
    if (n) {if(n>=(int)sizeof buf-1){out[0]=0;return 0;}buf[n++] = '/';}
    for (const char *p = a; *p; p++) {
        if(n>=(int)sizeof buf-1){out[0]=0;return 0;}
        buf[n++] = (*p == '\\') ? '/' : *p;
    }
    buf[n] = 0;

    const char *seg[32];
    int seglen[32], ns = 0;
    int i = 0;
    while (buf[i]) {
        while (buf[i] == '/') i++;
        if (!buf[i]) break;
        int s = i;
        while (buf[i] && buf[i] != '/') i++;
        int len = i - s;
        if (len == 1 && buf[s] == '.') continue;
        if (len == 2 && buf[s] == '.' && buf[s + 1] == '.') {
            if (ns) ns--;
            continue;
        }
        if(ns>=32){out[0]=0;return 0;}
        seg[ns] = buf + s; seglen[ns] = len; ns++;
    }

    int o = 0;
    for (int k = 0; k < ns; k++) {
        if (o && o < cap - 1) out[o++] = '/';
        for (int j = 0; j < seglen[k]; j++) {
            if (o >= cap - 1) { out[0] = 0; return 0; }
            out[o++] = seg[k][j];
        }
    }
    if (o >= cap) { out[0] = 0; return 0; }
    out[o] = 0;
    return 1;
}

static inline void sc_parent(const char *cwd, char *out, int cap)
{
    if (!out || cap < 1) return;
    if (!cwd) cwd = "";
    int last = -1;
    for (int i = 0; cwd[i]; i++)
        if (cwd[i] == '/') last = i;
    int n = last < 0 ? 0 : last;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = cwd[i];
    out[n] = 0;
}
