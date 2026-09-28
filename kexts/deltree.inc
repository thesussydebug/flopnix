/* Deletes a folder with everything in it, on the floppy or on USB. Include right after `api`. */
#pragma once

#define DT_LOADED (-4)
#define DT_DEPTH  8
#define DT_BATCH  16

/* Refuses folders that hold a loaded extension, so sys/ can't be emptied by accident. */
static inline int dt_holds_loaded(const char *dir)
{
    for (int i = 0; i < api->kext_count(); i++) {
        const KextInfo *k = api->kext_get(i);
        if (k && !k->status && fs_under(k->name, dir)) return 1;
    }
    return 0;
}

static inline int dt_floppy(const char *dir, int *count)
{
    if (dt_holds_loaded(dir)) return DT_LOADED;
    for (int i = 0; i < FS_NFILES; i++) {
        const FsEnt *e = api->fs_slot(i);
        if (!e || !e->used || !fs_under(e->name, dir)) continue;
        char nm[FS_NAMELEN];
        api->strlcpy(nm, e->name, sizeof nm);
        int r = api->fs_delete(nm);
        if (r) return r;
        (*count)++;
    }
    return api->fs_is_dir(dir) ? api->fs_delete(dir) : 0;
}

static inline int dt_usb(const char *path, int depth, int *count)
{
    if (depth > DT_DEPTH) return -1;
    FatEnt *list = api->kmalloc(DT_BATCH * sizeof *list);
    if (!list) return -2;
    int r = 0, n;
    while (!r && (n = api->fat_list(path, list, DT_BATCH)) > 0) {
        int gone = 0;
        for (int i = 0; i < n && !r; i++) {
            const char *nm = list[i].name;
            if (!api->strcmp(nm, ".") || !api->strcmp(nm, "..")) continue;
            char child[128];
            if (path[0] == '/' && !path[1]) api->kfmt(child, sizeof child, "/%s", nm);
            else api->kfmt(child, sizeof child, "%s/%s", path, nm);
            r = list[i].is_dir ? dt_usb(child, depth + 1, count) : api->fat_delete(child);
            if (!r) { gone++; (*count)++; }
        }
        if (!gone) break;
    }
    api->kfree(list);
    if (r) return r;
    return api->fat_rmdir(path);
}
