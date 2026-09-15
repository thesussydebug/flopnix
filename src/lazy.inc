/* Tracks apps that load when first opened. */
typedef struct { const char *file, *title, *cmds, *exts; int category; } LazySpec;
static const LazySpec lazy_specs[] = {
#include "appcatalog.inc"
};
#define NLAZY ((int)(sizeof lazy_specs / sizeof lazy_specs[0]))
static int lazy_types[NLAZY];
static u8 lazy_state[NLAZY];
static int lazy_register_type = -1;
int kext_lazy_type(void) { return lazy_register_type; }

static int lazy_file(const char *name)
{
    for (int i = 0; i < NLAZY; i++)
        if (!strcmp(name, lazy_specs[i].file)) return i;
    return -1;
}
static void lazy_draw(Win *w, int x, int y, int cw, int ch)
{ (void)w; (void)x; (void)y; (void)cw; (void)ch; }
static void lazy_size(int inst, int *w, int *h) { (void)inst; *w = 320; *h = 200; }
static int lazy_present(const char *name)
{
    for (int i = 0; i < FS_NFILES; i++) {
        FsEnt *e = fs_slot(i);
        if (e->used && !strcmp(e->name, name)) return 1;
    }
    return 0;
}
static void lazy_catalog(void)
{
    for (int i = 0; i < NLAZY; i++) {
        if (!lazy_present(lazy_specs[i].file)) continue;
        AppDesc d = {0};
        d.title = lazy_specs[i].title; d.max_inst = 1; d.in_menu = 1;
        d.draw = lazy_draw; d.client_size = lazy_size;
        d.category = lazy_specs[i].category;
        lazy_types[i] = register_app(&d) + 1;
    }
}
int app_ensure_loaded(int type)
{
    int owner=app_type_owner(type);
    if (owner>=0 && kexts[owner].status==46)
        return kext_load(kexts[owner].name)==0;
    if (owner>=0 && kexts[owner].status) return 0;
    for (int i = 0; i < NLAZY; i++) {
        if (lazy_types[i] != type + 1) continue;
        if (lazy_state[i] == 2) return 1;
        if (lazy_state[i] == 1) return app_type_owner(type) >= 0;
        int r = kext_load(lazy_specs[i].file);
        if (!r) return 1;
        char msg[96];
        kfmt(msg, sizeof msg, "%s: %s (E%d)", lazy_specs[i].title,
             r == 42 ? "not enough memory; close another app and retry" : "could not load from floppy", r);
        klog(msg); klog("\n"); fault_show_banner(msg);
        return 0;
    }
    return type >= 0 && type < app_count();
}
static int lazy_word(const char *words, const char *word)
{
    u32 n = strlen(word);
    while (*words) {
        const char *p = words;
        while (*words && *words != ' ') words++;
        if ((u32)(words - p) == n) {
            u32 j = 0;
            while (j < n && ((p[j] >= 'A' && p[j] <= 'Z') ? p[j] + 32 : p[j]) ==
                   ((word[j] >= 'A' && word[j] <= 'Z') ? word[j] + 32 : word[j])) j++;
            if (j == n) return 1;
        }
        if (*words) words++;
    }
    return 0;
}
static int lazy_hook(const char *word, int opener)
{
    for (int i = 0; i < NLAZY; i++)
        if (lazy_types[i] && lazy_word(opener ? lazy_specs[i].exts : lazy_specs[i].cmds, word))
            return app_ensure_loaded(lazy_types[i] - 1);
    return 1;
}
