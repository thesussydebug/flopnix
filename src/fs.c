/* Stores FLOPFS files on the boot floppy. */
#include "os.h"
#include "debug.h"
#include "fsplan.inc"
#include "fsdefrag.inc"
#include "fspath.inc"
#include "fscheck.inc"

#define FS_SUPER 288
#define FS_TABLE 289
#define FS_TSECT 10
#define FS_DATA  299
#define FS_END   2880
#define FS_MAGIC 0x53465046
#define FS_VER   2

static FsEnt table[FS_NFILES];
static int mounted;
static Mutex fs_mutex=MUTEX_INIT;
#include "fscache.inc"

static u32 entry_sectors(const FsEnt *e)
{
    u32 off = (const u8 *)e - (const u8 *)table;
    return (1u << (off / 512)) | (1u << ((off + sizeof *e - 1) / 512));
}

static int flush_table(u32 sectors)
{
    for (u32 i = 0; i < FS_TSECT; ) {
        if (!(sectors & (1u << i))) { i++; continue; }
        u32 first = i++;
        while (i < FS_TSECT && (sectors & (1u << i))) i++;
        if (fdc_write_many(FS_TABLE + first, (u8 *)table + first * 512, i - first)) return 0;
    }
    return 1;
}

static void reload_table(void)
{
    FdcResult result;fdc_result_get(&result);
    fs_cache_clear();mounted=0;fs_ensure();
    fdc_result_restore(&result);
}

static int commit_table(u32 sectors)
{
    if(flush_table(sectors))return 1;
    reload_table();return 0;
}

int fs_ensure(void)
{
    if (mounted) return mounted > 0;
    u8 sec[512];
    if (fdc_read(FS_SUPER, sec) != 0) { mounted = -1; return 0; }
    int ok = *(u32 *)sec == FS_MAGIC &&
             *(u32 *)(sec + 4) == FS_VER &&
             *(u32 *)(sec + 8) == sizeof(FsEnt);
    if(ok&&fdc_read_many(FS_TABLE,(u8 *)table,FS_TSECT))ok=0;
    if(!ok||!fs_table_valid(table,FS_NFILES,FS_DATA,FS_END)){
        memset(table,0,sizeof table);mounted=-1;
        klog("FLOPFS: metadata unreadable or invalid; disk left unchanged.\n");
        return 0;
    }
    mounted = 1;
    return 1;
}

FsEnt *fs_slot(int i)
{
    return (i >= 0 && i < FS_NFILES) ? &table[i] : 0;
}

static FsEnt *find(const char *name)
{
    for (int i = 0; i < FS_NFILES; i++)
        if (table[i].used && !strcmp(table[i].name, name))
            return &table[i];
    return 0;
}

int fs_exists(const char *name)
{
    return fs_ensure() && find(name) != 0;
}

static int alloc(int n)
{
    for (int start = FS_DATA; start + n <= FS_END; ) {
        int clash = 0;
        for (int i = 0; i < FS_NFILES; i++) {
            FsEnt *e = &table[i];
            if (!e->used) continue;
            if (start < e->start + e->nsect && e->start < start + n) {
                start = e->start + e->nsect;
                clash = 1;
                break;
            }
        }
        if (!clash) return start;
    }
    return -1;
}

static int fs_read_locked(const char *name, u8 *buf, u32 max)
{
    if (!fs_ensure()) return -1;
    FsEnt *e = find(name);
    if (!e) return -1;
    u32 size = e->size < max ? e->size : max;
    if(fc_read(e,buf,size))return (int)size;
    FsEnt saved=*e;e=&saved;u32 epoch=fc_epoch;
    u8 sec[512];
    u32 got = 0;
    while (size-got >= 512) {
        u32 n=(size-got)/512;
        if(fdc_read_many(e->start+got/512,buf+got,n))return FS_EIO;
        got+=n*512;
    }
    if (got < size) {
        if (fdc_read(e->start + got/512, sec) != 0) return FS_EIO;
        u32 n = size - got < 512 ? size - got : 512;
        memcpy(buf + got, sec, n);
        got += n;
    }
    if(epoch!=fc_epoch)return FS_EIO;
    fc_save(e,buf,size,epoch);
    return (int)size;
}

static int fs_write_locked(const char *name, const u8 *buf, u32 size, int fresh)
{

    if (!fs_name_ok(name)) return -1;
    if (!fs_ensure()) return -1;
    if (size > (u32)(FS_END - FS_DATA) * 512) return -2;
    int nsect = (size + 511) / 512;
    if (nsect == 0) nsect = 1;

    FsEnt *e = find(name);

    if (e && (e->attr & FS_ATTR_DIR)) return -3;

    FsEnt *grow = 0;
    FsEnt saved = {0};
    int existed = (e != 0);
    if (existed) saved = *e;
    if (e && (fresh || nsect > e->nsect)) { grow = e; e = 0; }
    if (!e) {
        int start = alloc(nsect);
        if (start < 0) return -2;
        if (grow) e = grow;
        else {
            int i;
            for (i = 0; i < FS_NFILES && table[i].used; i++) ;
            if (i == FS_NFILES) return -2;
            e = &table[i];
            memset(e, 0, sizeof *e);
            strlcpy(e->name, name, FS_NAMELEN);
            e->used = 1;
        }
        e->start = start;
        e->nsect = nsect;
    }
    e->size = size;
    e->mtime = rtc_now_dos();

    u32 full = size / 512;
    int failed = full && fdc_write_many(e->start, buf, full);
    if (!failed && (size % 512 || !size)) {
        u8 sec[512];
        memset(sec, 0, sizeof sec);
        if (size % 512) memcpy(sec, buf + full * 512, size % 512);
        failed = fdc_write(e->start + full, sec);
    }
    if (failed) {
        if (existed) *e = saved;
        else e->used = 0;
        return -1;
    }
    return commit_table(entry_sectors(e)) ? 0 : -1;
}

static int fs_delete_locked(const char *name)
{
    if (!fs_ensure()) return FS_EIO;
    FsEnt *e = find(name);
    if (!e) return -1;
    u32 sector = (u32)((u8 *)&e->used - (u8 *)table) / 512;
    e->used = 0;
    if (fdc_write(FS_TABLE + sector, (u8 *)table + sector * 512)) {
        reload_table();
        return FS_EIO;
    }
    return 0;
}

static int fs_mkdir_locked(const char *name)
{
    if (!fs_dirname_ok(name)) return -1;
    if (!fs_ensure()) return -1;
    if (find(name)) return -1;
    int i;
    for (i = 0; i < FS_NFILES && table[i].used; i++) ;
    if (i == FS_NFILES) return -2;
    FsEnt *e = &table[i];
    memset(e, 0, sizeof *e);
    strlcpy(e->name, name, FS_NAMELEN);
    e->used  = 1;
    e->attr  = FS_ATTR_DIR;
    e->size  = 0;
    e->nsect = 0;
    e->start = 0;
    e->mtime = rtc_now_dos();
    return commit_table(entry_sectors(e)) ? 0 : -1;
}

int fs_is_dir(const char *name)
{
    if (!fs_ensure()) return 0;
    FsEnt *e = find(name);
    return e && (e->attr & FS_ATTR_DIR);
}

static int fs_touch_locked(const char *name)
{
    if (!fs_ensure()) return FS_EIO;
    FsEnt *e = find(name);
    if (!e) return -1;
    e->mtime = rtc_now_dos();
    return commit_table(entry_sectors(e)) ? 0 : FS_EIO;
}

static int fs_rename_locked(const char *oldname, const char *newname)
{
    if (!fs_name_ok(newname)) return -1;
    if (!fs_ensure()) return -1;
    if (!strcmp(oldname, newname)) return 0;
    FsEnt *e = find(oldname);
    if (!e) return -1;
    if (find(newname)) return -1;
    int l = 0;
    while (newname[l]) l++;
    if (l >= FS_NAMELEN) return -1;
    strlcpy(e->name, newname, FS_NAMELEN);
    return commit_table(entry_sectors(e)) ? 0 : -1;
}

static int fs_rename_dir_locked(const char *olddir, const char *newdir)
{
    if (!fs_dirname_ok(newdir)) return -1;
    if (fs_under(newdir,olddir)) return -1;
    if (!fs_ensure()) return -1;
    if (!strcmp(olddir, newdir)) return 0;
    if (find(newdir)) return -1;
    for(int i=0;i<FS_NFILES;i++)
        if(table[i].used&&fs_under(table[i].name,newdir))return -1;

    char nn[FS_NAMELEN];
    int hits = 0;
    for (int i = 0; i < FS_NFILES; i++) {
        if (!table[i].used) continue;
        if (!strcmp(table[i].name, olddir)) { hits++; continue; }
        if (!fs_under(table[i].name, olddir)) continue;
        if (!fs_rejoin(table[i].name, olddir, newdir, nn, FS_NAMELEN)) return -2;
        if (find(nn)) return -1;
        hits++;
    }
    if (!hits) return -1;

    u32 sectors = 0;
    for (int i = 0; i < FS_NFILES; i++) {
        if (!table[i].used) continue;
        if (!strcmp(table[i].name, olddir)) {
            strlcpy(table[i].name, newdir, FS_NAMELEN);
            sectors |= entry_sectors(&table[i]);
            continue;
        }
        if (!fs_under(table[i].name, olddir)) continue;
        if (fs_rejoin(table[i].name, olddir, newdir, nn, FS_NAMELEN)) {
            strlcpy(table[i].name, nn, FS_NAMELEN);
            sectors |= entry_sectors(&table[i]);
        }
    }
    return commit_table(sectors) ? 0 : -1;
}

int fs_dir_count(const char *dir)
{
    if (!fs_ensure()) return 0;
    int n = 0;
    for (int i = 0; i < FS_NFILES; i++)
        if (table[i].used && fs_under(table[i].name, dir)) n++;
    return n;
}

static int dfg_rd(u32 lba, u8 *sec)
{
    for (int t = 0; t < 3; t++) {
        if (fdc_read(lba, sec) == 0) return 0;
        gui_pump();
    }
    return -1;
}
static int dfg_wr(u32 lba, const u8 *sec)
{
    for (int t = 0; t < 3; t++) {
        if (fdc_write(lba, sec) == 0) return 0;
        gui_pump();
    }
    return -1;
}

static int fs_defrag_locked(void (*prog)(int done, int total))
{
    if (!fs_ensure()) return -1;
    static FpEnt e[FS_NFILES];
    for (int i = 0; i < FS_NFILES; i++) {
        e[i].used = table[i].used && table[i].nsect != 0;
        e[i].start = table[i].start;
        e[i].sects = table[i].nsect;
        e[i].idx = (u8)i;
    }
    int moves = fs_plan(e, FS_NFILES, FS_DATA);
    if (!moves) return 0;

    int done = 0;
    u8 sec[512];
    for (int i = 0; i < FS_NFILES && e[i].used; i++) {
        if (e[i].nstart == e[i].start) continue;

        if (e[i].nstart < FS_DATA || e[i].start < FS_DATA) {
            klog("defrag: refused a plan below the data area\n");
            return -1;
        }
        debug_path(0,table[e[i].idx].name);
        int r = fsd_move(dfg_rd, dfg_wr,
                         e[i].start, e[i].nstart, e[i].sects, sec);
        if (r != FSD_MOVED) {
            klog("defrag stopped at ");
            klog(table[e[i].idx].name);
            klog(r == FSD_TORN ? " - file DAMAGED\n"
               : r == FSD_RESTORED ? " - file copied back, intact\n"
               : " - file untouched, intact\n");
            return r;
        }
        table[e[i].idx].start = e[i].nstart;

        int fl = 0;
        for (int t = 0; t < 3 && !(fl = flush_table(entry_sectors(&table[e[i].idx]))); t++) gui_pump();
        if (!fl) {

            klog("defrag: table flush failed after moving ");
            klog(table[e[i].idx].name);
            klog(" - do not reboot before it succeeds\n");
            return FSD_TORN;
        }
        if (prog) prog(++done, moves);
    }
    return moves;
}

u32 fs_free_kb(void)
{
    if (!fs_ensure()) return 0;
    u32 used = 0;
    for (int i = 0; i < FS_NFILES; i++)
        if (table[i].used) used += table[i].nsect;
    return (FS_END - FS_DATA - used) / 2;
}

const char *ext_type(const char *name)
{
    const char *dot = 0;
    for (const char *p = name; *p; p++)
        if (*p == '.') dot = p;
    if (!dot || !dot[1]) return "File";
    const char *e = dot + 1;
    if (!strcasecmp(e,"fpa") || !strcasecmp(e,"pz")) return "Archive";
    if (!strcasecmp(e, "txt") || !strcasecmp(e, "md") ||
        !strcasecmp(e, "log") || !strcasecmp(e, "cfg") ||
        !strcasecmp(e, "ini")) return "Text";
    if (!strcasecmp(e, "c") || !strcasecmp(e, "h")) return "C source";
    if (!strcasecmp(e, "asm") || !strcasecmp(e, "s")) return "Assembly";
    if (!strcasecmp(e, "sh") || !strcasecmp(e, "bat") ||
        !strcasecmp(e, "cmd")) return "Script";
    if (!strcasecmp(e, "bin") || !strcasecmp(e, "img") ||
        !strcasecmp(e, "dat")) return "Binary";
    if (!strcasecmp(e, "bmp") || !strcasecmp(e, "raw")) return "Image";
    if (!strcasecmp(e, "mid") || !strcasecmp(e, "midi")) return "Music";
    if (!strcasecmp(e, "kx")) return "KExt";
    return "File";
}

int fs_read(const char *name,u8 *buf,u32 max){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_read_locked(name,buf,max);debug_done(0,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_write(const char *name,const u8 *buf,u32 size){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_write_locked(name,buf,size,0);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_replace(const char *name,const u8 *buf,u32 size){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_write_locked(name,buf,size,1);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_delete(const char *name){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_delete_locked(name);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_mkdir(const char *name){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_mkdir_locked(name);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_touch(const char *name){mtx_lock(&fs_mutex);const char *old=debug_path(0,name);int r=fs_touch_locked(name);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_rename(const char *oldname,const char *newname){mtx_lock(&fs_mutex);const char *old=debug_path(0,oldname);int r=fs_rename_locked(oldname,newname);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_rename_dir(const char *olddir,const char *newdir){mtx_lock(&fs_mutex);const char *old=debug_path(0,olddir);int r=fs_rename_dir_locked(olddir,newdir);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}

int fs_defrag(void (*prog)(int,int)){mtx_lock(&fs_mutex);const char *old=debug_path(0,"defrag");int r=fs_defrag_locked(prog);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}
