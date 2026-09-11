/* Reads the older desktop shortcut list. */
static inline int dl_parse(const char *line, u32 *start, const char **name)
{
    *start = 0;
    *name = line;
    if (!line || !line[0]) return 0;
    u32 v = 0;
    const char *p = line;
    while (*p >= '0' && *p <= '9') v = v * 10 + (u32)(*p++ - '0');
    if (p == line || *p != '\t') return 1;
    if (!p[1]) return 0;
    *start = v;
    *name = p + 1;
    return 1;
}
