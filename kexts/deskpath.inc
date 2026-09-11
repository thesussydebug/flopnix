static inline const char *desk_leaf(const char *s)
{
    const char *p = s;
    while (*s) { if (*s == '/') p = s + 1; s++; }
    return p;
}
static inline int desk_member(const char *s)
{
    const char *p = "desktop/";
    while (*p) if (*s++ != *p++) return 0;
    if (!*s) return 0;
    while (*s) if (*s++ == '/') return 0;
    return 1;
}
static inline int desk_path(char *out, const char *leaf, int cap)
{
    int n = 0;
    while (leaf[n]) { if (leaf[n] == '/' || leaf[n] == ':' || leaf[n] == '\\') return 0; n++; }
    if (!n || n + 9 > cap || (n == 1 && leaf[0] == '.') ||
        (n == 2 && leaf[0] == '.' && leaf[1] == '.')) return 0;
    const char *p = "desktop/";
    int i = 0;
    while (*p) out[i++] = *p++;
    for (int j = 0; j <= n; j++) out[i + j] = leaf[j];
    return 1;
}
