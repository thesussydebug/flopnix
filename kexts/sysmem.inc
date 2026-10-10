#pragma once
#define SM_MAX 128
enum { SM_HELD = 1, SM_DISK = 2, SM_DIRTY = 4, SM_GONE = 8 };
typedef struct { char name[FS_NAMELEN]; u8 *data; u32 size, dsize, dmtime; u16 dstart; u8 state; } SmFile;
typedef struct {
    SmFile f[SM_MAX];
    void *(*alloc)(u32 size);
    void (*release)(void *p);
    FsEnt *(*slot)(int i);
    u32 (*mounts)(void);
    int (*same)(const FsEnt *e, const u8 *data);
    u32 seen, bytes;
    u8 *flight, *limbo;
    FsEnt *view;
    u8 on, home, busy, cfg_dirty, flush_due;
} SysMem;

static int sm_same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int sm_sys(const char *name)
{
    return name && name[0] == 's' && name[1] == 'y' && name[2] == 's' && name[3] == '/' && name[4];
}

static int sm_tree(const char *name)
{
    return name && name[0] == 's' && name[1] == 'y' && name[2] == 's' && (!name[3] || name[3] == '/');
}

static int sm_code(const char *name)
{
    int n = 0;
    while (name[n]) n++;
    return n > 3 && name[n - 3] == '.' && name[n - 2] == 'k' && name[n - 1] == 'x';
}

static int sm_slot(const SysMem *t, const char *name)
{
    for (int i = 0; i < SM_MAX; i++)
        if ((t->f[i].state & SM_HELD) && sm_same(t->f[i].name, name)) return i;
    return -1;
}

static int sm_find(const SysMem *t, const char *name)
{
    int i = sm_slot(t, name);
    return i >= 0 && !(t->f[i].state & SM_GONE) ? i : -1;
}

static int sm_count(const SysMem *t)
{
    int n = 0;
    for (int i = 0; i < SM_MAX; i++)
        if ((t->f[i].state & (SM_HELD | SM_GONE)) == SM_HELD) n++;
    return n;
}

static int sm_pending(const SysMem *t)
{
    int n = 0;
    for (int i = 0; i < SM_MAX; i++)
        if ((t->f[i].state & SM_HELD) && (t->f[i].state & (SM_DIRTY | SM_GONE))) n++;
    return n;
}

static void sm_unload(SysMem *t, SmFile *f)
{
    if (f->data) {
        t->bytes -= f->size;
        if (f->data == t->flight) t->limbo = f->data;
        else t->release(f->data);
    }
    f->data = 0;
    f->size = 0;
}

static void sm_drop(SysMem *t, int i)
{
    sm_unload(t, &t->f[i]);
    t->f[i].state = 0;
}

static int sm_put(SysMem *t, const char *name, const u8 *data, u32 size)
{
    int i = sm_slot(t, name), n = 0;
    while (name[n]) n++;
    if (n >= FS_NAMELEN) return -1;
    if (i < 0) for (i = 0; i < SM_MAX && t->f[i].state; i++) ;
    if (i == SM_MAX) return -2;
    SmFile *f = &t->f[i];
    if (!f->state || data != f->data) {
        u8 *copy = t->alloc(size);
        if (!copy) return -2;
        for (u32 k = 0; k < size; k++) copy[k] = data[k];
        if (f->state) sm_unload(t, f);
        else { f->dsize = f->dmtime = 0; f->dstart = 0; }
        f->data = copy;
        f->size = size;
        t->bytes += size;
    }
    for (int k = 0; k <= n; k++) f->name[k] = name[k];
    f->state = (u8)((f->state & (SM_DISK | SM_DIRTY)) | SM_HELD);
    return i;
}

static void sm_record(SmFile *f, const FsEnt *e)
{
    f->dsize = e->size;
    f->dmtime = e->mtime;
    f->dstart = e->start;
    f->state = (u8)((f->state & ~SM_DIRTY) | SM_DISK);
}

static const FsEnt *sm_entry(const SysMem *t, const char *name)
{
    for (int i = 0; i < FS_NFILES; i++) {
        const FsEnt *e = t->slot(i);
        if (!e) break;
        if (e->used && sm_same(e->name, name)) return e;
    }
    return 0;
}

static int sm_fill(SysMem *t, const FsEnt *e, const u8 *data)
{
    int i = sm_put(t, e->name, data, e->size);
    if (i < 0) return i;
    sm_record(&t->f[i], e);
    return 0;
}

static int sm_is_home(const SysMem *t)
{
    int want = 0;
    for (int i = 0; i < SM_MAX; i++) {
        const SmFile *f = &t->f[i];
        if ((f->state & (SM_HELD | SM_DISK)) != (SM_HELD | SM_DISK)) continue;
        const FsEnt *e = sm_entry(t, f->name);
        if (!e || e->size != f->dsize || e->mtime != f->dmtime || e->start != f->dstart) return 0;
        want++;
    }
    return want > 0;
}

static void sm_refresh(SysMem *t)
{
    u32 now = t->mounts();
    if (now == t->seen) return;
    t->seen = now;
    t->home = (u8)sm_is_home(t);
    if (t->home && (sm_pending(t) || t->cfg_dirty)) t->flush_due = 1;
}

static void sm_start(SysMem *t)
{
    t->seen = t->mounts();
    t->home = (u8)sm_is_home(t);
    if (!t->view) t->view = t->alloc((SM_MAX + 1) * sizeof(FsEnt));
    if (t->view) for (u32 i = 0; i < (SM_MAX + 1) * sizeof(FsEnt); i++) ((u8 *)t->view)[i] = 0;
    t->on = 1;
}

static u8 *sm_take(SysMem *t, int i, u32 *size)
{
    SmFile *f = &t->f[i];
    if ((f->state & (SM_HELD | SM_DIRTY | SM_GONE)) != (SM_HELD | SM_DIRTY) || !f->data) return 0;
    *size = f->size;
    return t->flight = f->data;
}

static inline void sm_land(SysMem *t)
{
    u8 *late = t->limbo;
    t->flight = t->limbo = 0;
    if (late) t->release(late);
}

static void sm_clear(SysMem *t)
{
    t->on = 0;
    for (int i = 0; i < SM_MAX; i++)
        if (t->f[i].state) sm_drop(t, i);
    t->home = t->cfg_dirty = t->flush_due = 0;
    if (t->view) t->release(t->view);
    t->view = 0;
}

static int sm_apart(const SysMem *t)
{
    return t->on && t->view && (t->busy || !t->home);
}

static void sm_view(SysMem *t, FsEnt **e, u32 i)
{
    if (!sm_apart(t)) return;
    FsEnt *real = *e;
    if (real && (!real->used || sm_tree(real->name))) real = 0;
    if (real) return;
    *e = &t->view[SM_MAX];
    if (i < FS_NFILES - SM_MAX || i >= FS_NFILES) return;
    i -= FS_NFILES - SM_MAX;
    const SmFile *f = &t->f[i];
    if ((f->state & (SM_HELD | SM_GONE)) != SM_HELD) return;
    FsEnt *v = &t->view[i];
    for (int k = 0; k < FS_NAMELEN; k++) {
        v->name[k] = f->name[k];
        if (!f->name[k]) break;
    }
    v->size = f->size;
    v->mtime = f->dmtime;
    v->nsect = (u16)((f->size + 511) / 512);
    v->used = 1;
    *e = v;
}

static int sm_under(const SysMem *t, const char *dir)
{
    int n = 0, d = 0;
    while (dir[d]) d++;
    for (int i = 0; i < SM_MAX; i++) {
        const SmFile *f = &t->f[i];
        if ((f->state & (SM_HELD | SM_GONE)) != SM_HELD) continue;
        int k = 0;
        while (k < d && f->name[k] == dir[k]) k++;
        if (k == d && f->name[d] == '/' && f->name[d + 1]) n++;
    }
    return n;
}

static int sm_rename(SysMem *t, int i, const char *to)
{
    SmFile *f = &t->f[i];
    int n = 0, j = sm_slot(t, to);
    while (to[n]) n++;
    if (n >= FS_NAMELEN || !sm_sys(to) || (j >= 0 && !(t->f[j].state & SM_GONE))) return -1;
    if (j < 0) {
        if (!(f->state & SM_DISK)) {
            for (int k = 0; k <= n; k++) f->name[k] = to[k];
            f->state |= SM_DIRTY;
            return 0;
        }
        for (j = 0; j < SM_MAX && t->f[j].state; j++) ;
        if (j == SM_MAX) return -2;
        t->f[j].dsize = t->f[j].dmtime = 0;
        t->f[j].dstart = 0;
    }
    SmFile *g = &t->f[j];
    if (g->state) sm_unload(t, g);
    for (int k = 0; k <= n; k++) g->name[k] = to[k];
    g->data = f->data;
    g->size = f->size;
    g->state = (u8)((g->state & SM_DISK) | SM_HELD | SM_DIRTY);
    f->data = 0;
    f->size = 0;
    if (f->state & SM_DISK) f->state |= SM_GONE;
    else f->state = 0;
    return 0;
}

static int sm_memory(SysMem *t, int op, const char *name, void *buf, u32 n)
{
    int i = sm_find(t, name);
    switch (op) {
    case FSH_READ:
        return -1;
    case FSH_WRITE:
    case FSH_REPLACE:
        if (sm_code(name)) return -1;
        if (n > FS_MAXFILE) return -2;
        i = sm_put(t, name, buf, n);
        if (i < 0) return i;
        t->f[i].state = (u8)((t->f[i].state & ~SM_GONE) | SM_DIRTY);
        return 0;
    case FSH_DELETE:
        if (i < 0) return -1;
        if (t->f[i].state & SM_DISK) { sm_unload(t, &t->f[i]); t->f[i].state |= SM_GONE; }
        else sm_drop(t, i);
        return 0;
    case FSH_MKDIR:
        return 0;
    case FSH_TOUCH:
        return i < 0 ? -1 : 0;
    case FSH_RENAME:
        return i < 0 ? -1 : sm_rename(t, i, buf);
    }
    return -1;
}

static void sm_done(SysMem *t, int op, const char *name, void *buf, int result)
{
    int i = sm_slot(t, name);
    if (result) return;
    if (op == FSH_DELETE) {
        if (i >= 0) sm_drop(t, i);
        return;
    }
    if (op == FSH_RENAME) {
        int n = 0, j = sm_slot(t, buf);
        while (((const char *)buf)[n]) n++;
        if (j >= 0 && j != i) sm_drop(t, j);
        if (i < 0) return;
        if (!sm_sys(buf) || n >= FS_NAMELEN) { sm_drop(t, i); return; }
        for (int k = 0; k <= n; k++) t->f[i].name[k] = ((const char *)buf)[k];
        name = buf;
    }
    const FsEnt *e = sm_entry(t, name);
    if (!e) return;
    if (op == FSH_WRITE || op == FSH_REPLACE) {
        if (i >= 0) t->f[i].state &= (u8)~SM_GONE;
        int j = t->same && !t->same(e, buf) ? -1 : sm_put(t, name, buf, e->size);
        if (j < 0) {
            if (i >= 0) sm_drop(t, i);
            return;
        }
        i = j;
    }
    if (i >= 0 && !(t->f[i].state & SM_GONE)) sm_record(&t->f[i], e);
}

static int sm_hook(SysMem *t, int op, const char *name, void *buf, u32 n)
{
    int phase = op & (FSH_LIVE | FSH_DONE);
    op &= 0x3F;
    if (op == FSH_CONFIG || op == FSH_KERNEL) {
        if (!t->on) return t->busy ? 0 : FSH_PASS;
        if (!t->busy && t->home && t->mounts() == t->seen) return FSH_PASS;
        if (op == FSH_KERNEL) return 0;
        t->cfg_dirty = 1;
        return 1;
    }
    if (op == FSH_SLOT) {
        sm_view(t, buf, n);
        return 0;
    }
    if (op == FSH_ISDIR || op == FSH_COUNT) {
        if (!sm_apart(t) || !sm_tree(name)) return FSH_PASS;
        int inside = sm_under(t, name);
        return op == FSH_COUNT ? inside : inside > 0;
    }
    int system = t->on && (sm_sys(name) || (op == FSH_MKDIR && sm_tree(name)));
    if (op == FSH_EXISTS) {
        if (!system) return t->busy ? 0 : FSH_PASS;
        int i = sm_slot(t, name);
        if (i >= 0) return !(t->f[i].state & SM_GONE);
        return t->home && !t->busy ? FSH_PASS : 0;
    }
    if (!system) return phase ? FSH_PASS : t->busy ? -1 : FSH_PASS;
    if (phase & FSH_DONE) {
        sm_done(t, op, name, buf, (int)n);
        return 0;
    }
    sm_refresh(t);
    if (t->limbo && buf == t->limbo) return 0;
    if (op == FSH_READ) {
        int i = sm_slot(t, name);
        if (i >= 0) {
            SmFile *f = &t->f[i];
            if (f->state & SM_GONE) return -1;
            u32 take = f->size < n ? f->size : n;
            for (u32 k = 0; k < take; k++) ((u8 *)buf)[k] = f->data[k];
            return (int)take;
        }
    }
    if (t->home && !t->busy) return FSH_PASS;
    return sm_memory(t, op, name, buf, n);
}
