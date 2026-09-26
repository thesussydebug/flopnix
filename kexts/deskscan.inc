/* Builds the desktop icon list from file-table entries. */
typedef struct {
    char (*name)[FS_NAMELEN];
    u8 *dir;
    short *dirs;
    int n, ndirs, cap, old_n, changed;
} DeskScan;

static inline int dk_same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static inline void dk_mark_dir(DeskScan *s, int k)
{
    if (s->dir[k]) return;
    s->dir[k] = 1;
    s->dirs[s->ndirs++] = (short)k;
}

static inline int dk_add(DeskScan *s, const char *path, int folder)
{
    char child[FS_NAMELEN];
    int deep = fs_child(path, "desktop", child, sizeof child);
    if (!deep) return -1;
    folder = folder || deep == 2;
    for (int j = 0; j < s->ndirs; j++)
        if (dk_same(s->name[s->dirs[j]], child)) return -1;
    if (folder)
        for (int k = 0; k < s->n; k++)
            if (dk_same(s->name[k], child)) { dk_mark_dir(s, k); return -1; }
    if (s->n >= s->cap) return -1;
    int k = s->n++;
    if (k >= s->old_n || !dk_same(s->name[k], child)) {
        int i = 0;
        for (; child[i]; i++) s->name[k][i] = child[i];
        s->name[k][i] = 0;
        s->changed = 1;
    }
    s->dir[k] = 0;
    if (folder) dk_mark_dir(s, k);
    return k;
}

static inline int dk_commit(const DeskScan *s, u8 *shown)
{
    int changed = s->changed || s->n != s->old_n;
    for (int k = 0; k < s->n; k++)
        if (shown[k] != s->dir[k]) { shown[k] = s->dir[k]; changed = 1; }
    return changed;
}
