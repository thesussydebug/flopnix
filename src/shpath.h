#ifndef SHPATH_H
#define SHPATH_H

static inline int sh_ci_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

static inline void sh_norm_path(const char *in, char *out, int cap)
{
    char tmp[128];
    int n = 0;
    for (const char *p = in; *p && n < (int)sizeof tmp - 1; p++)
        tmp[n++] = (*p == '\\') ? '/' : *p;
    tmp[n] = 0;

    const char *s = tmp;
    while (*s == ' ' || *s == '\t') s++;
    for (const char *p = s; *p && *p != '/'; p++)
        if (*p == ':') { s = p + 1; break; }
    {
        const char *fp = "floppy/";
        int k = 0;
        while (fp[k] && sh_ci_eq(s[k], fp[k])) k++;
        if (!fp[k]) s += k;
    }
    while (*s == '/') s++;

    int o = 0;
    while (*s && o < cap - 1) out[o++] = *s++;
    out[o] = 0;
}

static inline int sh_spec_split(const char *spec, int *drive, char *out, int cap)
{
    int d = 0, tagged = 0;
    if (spec && spec[0] && spec[1] == ':') {
        if (spec[0] == 'u' || spec[0] == 'U') { d = 1; tagged = 1; spec += 2; }
        else if (spec[0] == 'a' || spec[0] == 'A') { tagged = 1; spec += 2; }
    }
    if (drive) *drive = d;
    int o = 0;
    if (spec) while (spec[o] && o < cap - 1) { out[o] = spec[o]; o++; }
    if (cap > 0) out[o] = 0;
    return tagged;
}

static inline int sh_is_system_file(const char *name)
{
    if (!name) return 0;
    return sh_ci_eq(name[0], 's') && sh_ci_eq(name[1], 'y') &&
           sh_ci_eq(name[2], 's') && name[3] == '/';
}

static inline void sh_spec_make(int drive, const char *path, char *out, int cap)
{
    if (cap <= 0) return;
    int o = 0;
    if (cap > 2) { out[o++] = drive ? 'u' : 'a'; out[o++] = ':'; }
    if (drive && path && path[0] != '/' && o < cap - 1) out[o++] = '/';
    if (path) while (*path && o < cap - 1) out[o++] = *path++;
    out[o] = 0;
}

#endif
