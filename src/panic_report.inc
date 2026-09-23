/* Formats the saved crash report. */
static __attribute__((noinline,minsize,unused)) const char *panic_pf_reason(u32 err)
{
    if (err & 8) return "Invalid page-table bits";
    if (err & 16) return (err & 1) ? "Execute: access denied" : "Execute: page not present";
    if (err & 2) return (err & 1) ? "Write: access denied" : "Write: page not present";
    return (err & 1) ? "Read: access denied" : "Read: page not present";
}

static __attribute__((noinline,minsize)) int pr_put(char *o, int cap, int at, const char *s)
{
    for (; s && *s && at < cap - 1; s++) o[at++] = *s;
    if (cap > 0) o[at] = 0;
    return at;
}

static __attribute__((noinline,minsize)) int pr_hex(char *o, int cap, int at, u32 v)
{
    for (int sh = 28; sh >= 0 && at < cap - 1; sh -= 4)
        o[at++] = "0123456789abcdef"[(v >> sh) & 0xF];
    if (cap > 0) o[at] = 0;
    return at;
}

static __attribute__((noinline,minsize)) int pr_dec(char *o, int cap, int at, u32 v)
{
    char t[11];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n-- && at < cap - 1) o[at++] = t[n];
    if (cap > 0) o[at] = 0;
    return at;
}

static __attribute__((noinline,minsize,unused)) int panic_report_fmt(char *out, int cap,
                                   const char *release, u32 vec,
                                   const char *vecname, u32 err, u32 eip,
                                   const char *owner, const char *location, int is_pf, u32 cr2,
                                   u32 recoveries, const char *events,
                                   const char *log)
{
    if (cap <= 0) return 0;
    int at = 0;
    at = pr_put(out, cap, at, "FLOPNIX kernel panic\n");
    at = pr_put(out, cap, at, release);
    at = pr_put(out, cap, at, "\ncode P");
    at = pr_dec(out, cap, at, vec);
    at = pr_put(out, cap, at, " (");
    at = pr_put(out, cap, at, vecname);
    at = pr_put(out, cap, at, ") err ");
    at = pr_hex(out, cap, at, err);
    at = pr_put(out, cap, at, "\nInstruction address: ");
    at = pr_hex(out, cap, at, eip);
    at = pr_put(out, cap, at, "  owner ");
    at = pr_put(out, cap, at, owner ? owner : "kernel");
    at = pr_put(out, cap, at, "\nlocation ");
    at = pr_put(out, cap, at, location);
    if (is_pf) {
        at = pr_put(out, cap, at, "\nMemory address: ");
        at = pr_hex(out, cap, at, cr2);
        at = pr_put(out, cap, at, "\n");
        at = pr_put(out, cap, at, panic_pf_reason(err));
    }
    at = pr_put(out, cap, at, "\nfaults recovered: ");
    at = pr_dec(out, cap, at, recoveries);

    at = pr_put(out, cap, at, "\n--- events ---\n");
    at = pr_put(out, cap, at, events);
    at = pr_put(out, cap, at, "\n--- kernel log ---\n");
    at = pr_put(out, cap, at, log);
    if (at < cap - 1 && (at == 0 || out[at - 1] != '\n')) out[at++] = '\n';
    out[at] = 0;
    return at;
}
