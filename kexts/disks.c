#include "kapi.h"
#include "floppy.h"
#include "disktools.h"
#include "sysmem.inc"
#include "copymap.inc"

static const Kapi *api;
static SysMem mem;
static u8 *image, *map;
static u32 image_sectors;
static int image_files, posted;

static u32 irq_off(void) { u32 f; __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory"); return f; }
static void irq_back(u32 f) { __asm__ volatile("pushl %0; popfl" : : "r"(f) : "memory", "cc"); }

static u32 mounts(void) { return api->disk_stat(DS_MOUNTS); }
static void *take(u32 size) { return api->kmalloc(size ? size : 1); }
static void give(void *p) { api->kfree(p); }
static FsEnt *table;
static int peeking, mounted;
static FsEnt *entry(int i)
{
    if (!i) {
        peeking++;
        mounted = api->fs_slot(0) != 0;
        peeking--;
    }
    return mounted && table && i >= 0 && i < FS_NFILES ? table + i : 0;
}
static int hook(int op, const char *name, void *buf, u32 n)
{
    if (op == FSH_SLOT) {
        if (*(FsEnt **)buf) table = *(FsEnt **)buf - n;
        if (peeking) return 0;
    }
    return sm_hook(&mem, op, name, buf, n);
}

static int same(const FsEnt *e, const u8 *data)
{
    const FloppyOps *d = api->service_get("disk.floppy");
    u32 sectors = (e->size + 511) / 512, k = 0;
    if (!e->size || !d || d->abi < FLOPPY_ABI) return 1;
    u8 *got = api->kmalloc(sectors * 512);
    if (!got) return 0;
    if (!d->read(e->start, got, sectors))
        while (k < e->size && got[k] == data[k]) k++;
    api->kfree(got);
    return k == e->size;
}

static void probe(void)
{
    u8 b;
    if (mem.on) api->fs_read("sys/?", &b, 0);
}

static void settle(void)
{
    api->disk_media();
    probe();
}

static int flush(void)
{
    char name[FS_NAMELEN];
    if (!mem.on || !mem.home) return sm_pending(&mem) + mem.cfg_dirty;
    mem.flush_due = 0;
    for (int i = 0; i < SM_MAX; i++) {
        u32 size = 0, f = irq_off();
        u8 state = mem.f[i].state, *data = sm_take(&mem, i, &size);
        api->strlcpy(name, mem.f[i].name, sizeof name);
        irq_back(f);
        if (!(state & SM_HELD)) continue;
        if (data) {
            api->fs_write(name, data, size);
            f = irq_off();
            u8 *late = mem.limbo;
            mem.flight = mem.limbo = 0;
            irq_back(f);
            if (late) api->kfree(late);
        } else if (state & SM_GONE) {
            if (api->fs_delete(name) == -1 && mem.home) {
                f = irq_off();
                if ((mem.f[i].state & SM_GONE) && !api->strcmp(mem.f[i].name, name)) mem.f[i].state = 0;
                irq_back(f);
            }
        }
    }
    if (mem.cfg_dirty && mem.home) {
        mem.cfg_dirty = 0;
        if (!api->config_save()) mem.cfg_dirty = 1;
    }
    return sm_pending(&mem) + mem.cfg_dirty;
}

static void job(void *ctx)
{
    (void)ctx;
    probe();
    if (mem.flush_due) {
        int before = sm_pending(&mem) + mem.cfg_dirty, left = flush();
        char line[96];
        if (left) api->kfmt(line, sizeof line, "System disk: %d of %d changes could not be saved.", left, before);
        else api->kfmt(line, sizeof line, "System disk: %d changes saved.", before);
        if (before) api->notify(line);
    }
    posted = 0;
}

static void tick(void *ctx)
{
    (void)ctx;
    if (!mem.on || mem.busy || posted) return;
    if (!mem.flush_due && mounts() == mem.seen) return;
    posted = 1;
    if (api->work_post(job, 0) < 0) posted = 0;
}

static void leaving(void)
{
    if (!mem.on || !(sm_pending(&mem) + mem.cfg_dirty)) return;
    settle();
    flush();
}

static int state(int *files, u32 *bytes, int *waiting)
{
    probe();
    if (files) *files = sm_count(&mem);
    if (bytes) *bytes = mem.bytes;
    if (waiting) *waiting = sm_pending(&mem) + mem.cfg_dirty;
    return (mem.on ? DT_ON : 0) | (mem.on && mem.home ? DT_HOME : 0) | (image ? DT_COPY : 0);
}

static void status(void (*print)(const char *))
{
    char line[96];
    int waiting = sm_pending(&mem) + mem.cfg_dirty;
    if (!mem.on) { print("system files   on the floppy\n"); return; }
    api->kfmt(line, sizeof line, "system files   in memory, %d files, %u KB\n", sm_count(&mem), (mem.bytes + 1023) / 1024);
    print(line);
    print(mem.home ? "system disk    in the drive\n" : "system disk    not in the drive\n");
    if (waiting) {
        api->kfmt(line, sizeof line, "not saved yet  %d\n", waiting);
        print(line);
    }
}

static int load(void (*print)(const char *))
{
    char line[96];
    u32 total = 0, largest = 0;
    int files = 0, done = 0, bad = 0;
    if (api->disk_media() != 3) { print("no FLOPNIX disk in the drive\n"); return 0; }
    for (int i = 0; i < FS_NFILES; i++) {
        const FsEnt *e = entry(i);
        if (!e) break;
        if (!e->used || (e->attr & FS_ATTR_DIR) || !sm_sys(e->name)) continue;
        files++;
        total += e->size;
        if (e->size > largest) largest = e->size;
    }
    if (!files || files > SM_MAX) { print(files ? "too many system files\n" : "no system files on this disk\n"); return 0; }
    u8 *spare = api->kmalloc(262144), *work = api->kmalloc(largest + 1);
    api->esc_arm();
    for (int i = 0; spare && work && i < FS_NFILES && !bad; i++) {
        const FsEnt *e = entry(i);
        if (!e) { bad = 1; break; }
        if (!e->used || (e->attr & FS_ATTR_DIR) || !sm_sys(e->name)) continue;
        FsEnt copy;
        api->memcpy(&copy, e, sizeof copy);
        api->busy_set("System files to memory", copy.name, done * 256 / files);
        if (api->esc_pending()) bad = 3;
        else if (copy.size > largest || api->fs_read(copy.name, work, copy.size + 1) != (int)copy.size) bad = 1;
        else if (sm_fill(&mem, &copy, work)) bad = 2;
        done++;
    }
    api->busy_end();
    int short_memory = !spare || !work || bad == 2;
    api->kfree(spare);
    api->kfree(work);
    if (!short_memory && !bad) {
        sm_start(&mem);
        if (mem.home) return 1;
        bad = 1;
    }
    sm_clear(&mem);
    if (short_memory) api->kfmt(line, sizeof line, "not enough memory: %d system files need %u KB\n", files, (total + 1023) / 1024 + 256);
    else api->strlcpy(line, bad == 3 ? "stopped\n" : "could not read the system files\n", sizeof line);
    print(line);
    return 0;
}

static void sysmem(const char *args, void (*print)(const char *))
{
    char line[96];
    while (*args == ' ') args++;
    if (mem.busy) { print("a disk copy is running\n"); return; }
    if (!api->strcmp(args, "on")) {
        if (!mem.on && !load(print)) return;
    } else if (!api->strcmp(args, "off")) {
        if (mem.on) {
            settle();
            int left = flush();
            if (left) {
                api->kfmt(line, sizeof line, "%d changes are only in memory; put the system disk in\n", left);
                print(line);
                return;
            }
            sm_clear(&mem);
        }
    } else if (*args) {
        print("usage: sysmem [on|off]\n");
        return;
    } else if (mem.on) {
        settle();
        if (mem.flush_due) flush();
    }
    status(print);
}

static void drop(void)
{
    api->kfree(image);
    api->kfree(map);
    image = map = 0;
    image_sectors = 0;
}

static const FloppyOps *drive(void)
{
    const FloppyOps *d = api->service_get("disk.floppy");
    return d && d->abi >= FLOPPY_ABI_WRITE ? d : 0;
}

static int pass(const FloppyOps *d, int writing, u32 *failed)
{
    u32 at = 0, index = 0, n;
    int r = 0;
    mem.busy = 1;
    api->esc_arm();
    while (!r && (n = cm_run(map, &at, FLOPPY_TRACK_SECTORS - at % FLOPPY_TRACK_SECTORS)) != 0) {
        u32 room = FLOPPY_TRACK_SECTORS - at % FLOPPY_TRACK_SECTORS;
        u8 *p = image + index * 512;
        if (n > room) n = room;
        api->busy_set("Disk copy", writing ? "writing the target disk" : "reading the source disk", (int)(index * 256 / image_sectors));
        if (api->esc_pending()) r = 2;
        else if (writing ? d->write(at, p, n) || d->verify(at, p, n) : d->read(at, p, n)) r = 1;
        *failed = at;
        at += n;
        index += n;
    }
    api->busy_end();
    mem.busy = 0;
    return r;
}

static void copy_read(const FloppyOps *d, void (*print)(const char *))
{
    char line[96];
    u32 failed = 0;
    drop();
    if (mem.on) { settle(); flush(); }
    int media = api->disk_media();
    if (!media) { print("no disk in the drive\n"); return; }
    map = api->kmalloc(CM_SECTORS);
    if (map) image_sectors = cm_build(map, media == 3 ? entry : 0, FS_NFILES);
    image_files = 0;
    for (int i = 0; media == 3 && i < FS_NFILES; i++) {
        const FsEnt *e = entry(i);
        if (e && e->used && !(e->attr & FS_ATTR_DIR)) image_files++;
    }
    if (map) image = api->kmalloc(image_sectors * 512);
    if (!image) {
        api->kfmt(line, sizeof line, "not enough memory: this disk needs %u KB\n", map ? image_sectors / 2 : 3u);
        drop();
        print(line);
        return;
    }
    int r = pass(d, 0, &failed);
    if (r) {
        drop();
        if (r == 2) api->strlcpy(line, "stopped\n", sizeof line);
        else api->kfmt(line, sizeof line, "read error at sector %u\n", failed);
        print(line);
        return;
    }
    if (media == 3) api->kfmt(line, sizeof line, "%u sectors in memory: FLOPNIX disk, %d files\n", image_sectors, image_files);
    else api->kfmt(line, sizeof line, "%u sectors in memory: whole disk\n", image_sectors);
    print(line);
    print("put the target disk in, then: diskcopy write\n");
}

static void copy_write(const FloppyOps *d, int over, void (*print)(const char *))
{
    char line[96];
    u32 failed = 0;
    int files = 0;
    if (!image) { print("nothing in memory: run diskcopy with the source disk in\n"); return; }
    int media = api->disk_media();
    if (!media) { print("no disk in the drive\n"); return; }
    for (int i = 0; media == 3 && i < FS_NFILES; i++) {
        const FsEnt *e = entry(i);
        if (e && e->used && !(e->attr & FS_ATTR_DIR)) files++;
    }
    if (files && !over) {
        api->kfmt(line, sizeof line, "the disk in the drive holds %d files; diskcopy write over replaces them\n", files);
        print(line);
        return;
    }
    int r = pass(d, 1, &failed);
    if (image_sectors > 288) api->disk_write(288, image + cm_index(map, 288) * 512);
    if (r) {
        if (r == 2) api->strlcpy(line, "stopped: the target disk is incomplete\n", sizeof line);
        else api->kfmt(line, sizeof line, "write error at sector %u: the target disk is incomplete\n", failed);
        print(line);
        return;
    }
    api->kfmt(line, sizeof line, "%u sectors written and checked\n", image_sectors);
    drop();
    print(line);
}

static void diskcopy(const char *args, void (*print)(const char *))
{
    char line[96];
    const FloppyOps *d = drive();
    while (*args == ' ') args++;
    if (!d) { print("diskcopy needs a newer kernel\n"); return; }
    if (!*args) copy_read(d, print);
    else if (!api->strcmp(args, "write")) copy_write(d, 0, print);
    else if (!api->strcmp(args, "write over")) copy_write(d, 1, print);
    else if (!api->strcmp(args, "drop")) {
        api->kfmt(line, sizeof line, "%u KB released\n", image_sectors / 2);
        drop();
        print(line);
    } else print("usage: diskcopy [write [over]|drop]\n");
}

static const DiskToolsOps tools = { DISK_TOOLS_ABI, sysmem, diskcopy, state };

const KextHeader kext_header = { KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, KEXT_ON_DEMAND, "Disk tools" };

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    mem.alloc = take;
    mem.release = give;
    mem.slot = entry;
    mem.mounts = mounts;
    mem.same = same;
    if (api->register_service("disk.tools", &tools) < 0 || api->timer_add(100, tick, 0) < 0) return 1;
    api->register_shutdown(leaving);
    *api->fs_hook = hook;
    api->fs_slot(0);
    return 0;
}
