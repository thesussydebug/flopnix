static inline const char *mn_base(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/') b = q + 1;
    return b;
}

static inline void mn_display(const char *raw, int from_usb, char *out, int cap)
{
    if (cap <= 0) return;
    const char *p = raw;
    int usb = from_usb;
    if (from_usb < 0) {
        usb = 0;
        if (p[0] && p[1] == ':') {
            if (p[0] == 'u' || p[0] == 'U') usb = 1;
            p += 2;
        }
    }
    const char *tag = usb ? "USB: " : "A: ";
    const char *name = mn_base(p);
    int o = 0;
    for (const char *t = tag; *t && o < cap - 1; t++) out[o++] = *t;
    for (const char *n = name; *n && o < cap - 1; n++) out[o++] = *n;
    out[o] = 0;
}
