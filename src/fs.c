/* Stores FLOPFS files on the boot floppy. */
#include "os.h"
#include "debug.h"
#include "floppy.h"
#include "fsplan.inc"
#include "fsdefrag.inc"
#include "fspath.inc"
#include "fscheck.inc"

#define FS_SUPER 288
#define FS_TABLE 289
#define FS_TSECT (FS_NFILES * sizeof(FsEnt) / 512)
#define FS_DATA  fs_data
#define FS_END   2880
#define FS_MAGIC 0x53465046
#define FS_VER   3

static FsEnt table[FS_NFILES];
typedef struct { char name[24]; u32 size,mtime; u16 start,nsect; u8 used,attr,pad[2]; } FsLegacy;
static FsLegacy legacy[128];
static u32 fs_data=FS_TABLE+FS_TSECT, fs_tsect=FS_TSECT;
static int fs_slots=FS_NFILES, fs_namelen=FS_NAMELEN;
typedef struct { u8 bits[FS_TSECT]; } FsSectors;
static int mounted;
u32 fs_mounts;
static u16 bad[FB_MAX];
static int nbad;
static Mutex fs_mutex=MUTEX_INIT;
FsHook fs_hook;
#include "fscache.inc"

int fs_hooked(int op,const char *a,const void *b,u32 n)
{
    return fs_hook?fs_hook(op,a,(void *)b,n):FSH_PASS;
}

static void mark_entry(FsSectors *s,const FsEnt *e)
{
    u32 size=fs_slots==128?sizeof(FsLegacy):sizeof(FsEnt);
    u32 off=(e-table)*size;
    s->bits[off/512]=s->bits[(off+size-1)/512]=1;
}

static FsSectors entry_sectors(const FsEnt *e)
{
    FsSectors s={0};mark_entry(&s,e);return s;
}

static int flush_table(FsSectors sectors)
{
    const u8 *raw=(const u8 *)table;
    if(fs_slots==128){
        for(int i=0;i<128;i++){
            memcpy(legacy[i].name,table[i].name,24);
            memcpy(&legacy[i].size,&table[i].size,16);
        }
        raw=(const u8 *)legacy;
    }
    for(u32 i=0;i<fs_tsect;){
        if(!sectors.bits[i]){i++;continue;}
        u32 first=i++;
        while(i<fs_tsect&&sectors.bits[i])i++;
        if(fdc_write_many(FS_TABLE+first,raw+first*512,i-first))return 0;
    }
    return 1;
}

static void reload_table(void)
{
    FdcResult result;fdc_result_get(&result);
    fs_cache_clear();mounted=0;fs_ensure();
    fdc_result_restore(&result);
}

static int commit_table(FsSectors sectors)
{
    if(flush_table(sectors))return 1;
    reload_table();return 0;
}

int fs_ensure(void)
{
    if (fdc_changed) return 0;
    if (mounted) return mounted > 0;
    fs_mounts++;
    u8 sec[512];
    if (fdc_read(FS_SUPER, sec) != 0) { mounted = -1; return 0; }
    int old=*(u32 *)(sec+4)==2&&*(u32 *)(sec+8)==sizeof(FsLegacy);
    int ok=*(u32 *)sec==FS_MAGIC&&(old||
        (*(u32 *)(sec+4)==FS_VER&&*(u32 *)(sec+8)==sizeof(FsEnt)));
    fs_slots=old?128:FS_NFILES;fs_namelen=old?24:FS_NAMELEN;
    fs_tsect=old?10:FS_TSECT;fs_data=FS_TABLE+fs_tsect;
    nbad=ok?fb_parse(sec,bad,fs_data,FS_END):0;
    memset(table,0,sizeof table);
    if(ok&&fdc_read_many(FS_TABLE,old?(u8 *)legacy:(u8 *)table,fs_tsect))ok=0;
    if(ok&&old)for(int i=0;i<128;i++){
        if(legacy[i].used){
            int n=0;while(n<24&&legacy[i].name[n])n++;
            if(n==24){ok=0;break;}
        }
        memcpy(table[i].name,legacy[i].name,24);
        memcpy(&table[i].size,&legacy[i].size,16);
    }
    if(!ok||!fs_table_valid(table,FS_NFILES,FS_DATA,FS_END)){
        memset(table,0,sizeof table);mounted=-1;nbad=0;
        klog("FLOPFS: metadata unreadable or invalid; disk left unchanged.\n");
        return 0;
    }
    mounted = 1;
    return 1;
}

static int fs_fresh(void)
{
    if (fdc_changed) { fdc_changed = 0; fs_cache_clear(); mounted = 0; }
    return fs_ensure();
}

static int fs_media_locked(void)
{
    u8 sec[512];
    if (mounted && !fdc_changed && !fdc_peek()) return mounted > 0 ? 3 : 2;
    fdc_read(0, sec);
    if (mounted < 0) mounted = 0;
    return fs_fresh() ? 3 : !fdc_read(0, sec) << 1;
}

int fs_media(void)
{
    mtx_lock(&fs_mutex);
    int r = fs_media_locked();
    mtx_unlock(&fs_mutex);
    return r;
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

u32 fs_name_cap(void) { return (u32)fs_namelen; }

int fs_exists(const char *name)
{
    int h = fs_hooked(FSH_EXISTS, name, 0, 0);
    return h != FSH_PASS ? h : fs_ensure() && find(name) != 0;
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
        int b = clash ? -1 : fb_clash(bad, nbad, start, n);
        if (b >= 0) start = b + 1;
        else if (!clash) return start;
    }
    return -1;
}

static int bad_flush(void)
{
    u8 sec[512];
    if (fdc_read(FS_SUPER, sec) || *(u32 *)sec != FS_MAGIC) return 0;
    fb_store(sec, bad, nbad);
    return !fdc_write(FS_SUPER, sec);
}

static int bad_mark(u32 lba)
{
    if (lba < FS_DATA || lba >= FS_END || fb_add(bad, &nbad, lba) <= 0) return 0;
    if (bad_flush()) return 1;
    fb_del(bad, &nbad, lba);
    return 0;
}

static int fs_probe(u32 start, u32 count, const u8 *buf, u32 full, const u8 *tail)
{
    FdcResult r;
    u8 sec[512];
    int marked = 0;
    fdc_result_get(&r);
    u32 lo = r.lba, hi = lo + FLOPPY_TRACK_SECTORS - lo % FLOPPY_TRACK_SECTORS;
    if (((r.st[0] & 0xC8) != 0x40 && !(r.st[1] & 0x20)) || (r.st[1] & 2)) return 0;
    if (lo < start || lo - start >= count) return 0;
    if (hi > start + count) hi = start + count;
    for (u32 s = lo; s < hi; s++) {
        int fail;
        if (buf) {
            const u8 *p = s - start < full ? buf + (s - start) * 512 : tail;
            fail = fdc_write(s, p) || fdc_verify(s, p, 1);
        } else fail = fdc_read(s, sec);
        if (fail) marked += bad_mark(s);
    }
    return marked;
}

static int put(u32 lba, const u8 *buf, u32 n)
{
    if (fdc_write_many(lba, buf, n)) return -1;
    return nbad ? fdc_verify(lba, buf, n) : 0;
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
        if(fdc_read_many(e->start+got/512,buf+got,n))goto failed;
        got+=n*512;
    }
    if (got < size) {
        if (fdc_read(e->start + got/512, sec) != 0) goto failed;
        u32 n = size - got < 512 ? size - got : 512;
        memcpy(buf + got, sec, n);
        got += n;
    }
    if(epoch!=fc_epoch)return FS_EIO;
    fc_save(e,buf,size,epoch);
    return (int)size;
failed:
    fs_probe(e->start, e->nsect, 0, 0, 0);
    return FS_EIO;
}

static int fs_write_locked(const char *name, const u8 *buf, u32 size, int fresh)
{

    if (!fs_name_ok(name)) return -1;
    if (!fs_ensure()) return -1;
    if (strlen(name)>=(u32)fs_namelen) return -1;
    if (size > (u32)(FS_END - FS_DATA) * 512) return -2;
    int nsect = (size + 511) / 512;
    if (nsect == 0) nsect = 1;
    u32 full = size / 512;
    u8 tail[512];
    memset(tail, 0, sizeof tail);
    memcpy(tail, buf + full * 512, size % 512);

    for (int pass = 0; ; pass++) {
        FsEnt *e = find(name);

        if (e && (e->attr & FS_ATTR_DIR)) return -3;

        FsEnt *grow = 0;
        FsEnt saved = {0};
        int existed = (e != 0);
        if (existed) saved = *e;
        if (e && (fresh || nsect > e->nsect ||
                  fb_clash(bad, nbad, e->start, e->nsect) >= 0)) { grow = e; e = 0; }
        if (!e) {
            int start = alloc(nsect);
            if (start < 0) return -2;
            if (grow) e = grow;
            else {
                int i;
                for (i = 0; i < fs_slots && table[i].used; i++) ;
                if (i == fs_slots) return -2;
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

        int failed = full && put(e->start, buf, full);
        if (!failed && (int)full < nsect) failed = put(e->start + full, tail, 1);
        if (!failed) return commit_table(entry_sectors(e)) ? 0 : -1;

        u32 at = e->start;
        if (existed) *e = saved;
        else e->used = 0;
        if (!fs_probe(at, nsect, buf, full, tail) || pass == 7) return -1;
        fresh = 1;
    }
}

static int fs_delete_locked(const char *name)
{
    if (!fs_ensure()) return FS_EIO;
    FsEnt *e = find(name);
    if (!e) return -1;
    e->used = 0;
    return commit_table(entry_sectors(e)) ? 0 : FS_EIO;
}

static int fs_mkdir_locked(const char *name)
{
    if (!fs_dirname_ok(name)) return -1;
    if (!fs_ensure()) return -1;
    if (strlen(name)+2>=(u32)fs_namelen) return -1;
    if (find(name)) return -1;
    int i;
    for (i = 0; i < fs_slots && table[i].used; i++) ;
    if (i == fs_slots) return -2;
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
    int h = fs_hooked(FSH_ISDIR, name, 0, 0);
    if (h != FSH_PASS) return h;
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
    FsEnt *e = find(oldname);
    if (!e) return -1;
    if (!strcmp(oldname, newname)) return 0;
    if (find(newname)) return -1;
    int l = 0;
    while (newname[l]) l++;
    if (l >= fs_namelen) return -1;
    strlcpy(e->name, newname, FS_NAMELEN);
    return commit_table(entry_sectors(e)) ? 0 : -1;
}

static int fs_rename_dir_locked(const char *olddir, const char *newdir)
{
    if (!fs_dirname_ok(newdir)) return -1;
    if (fs_under(newdir,olddir)) return -1;
    if (!fs_ensure()) return -1;
    if (strlen(newdir)+2>=(u32)fs_namelen) return -1;
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
        if (!fs_rejoin(table[i].name, olddir, newdir, nn, fs_namelen)) return -2;
        if (find(nn)) return -1;
        hits++;
    }
    if (!hits) return -1;

    FsSectors sectors = {0};
    for (int i = 0; i < FS_NFILES; i++) {
        if (!table[i].used) continue;
        if (!strcmp(table[i].name, olddir)) {
            strlcpy(table[i].name, newdir, FS_NAMELEN);
            mark_entry(&sectors,&table[i]);
            continue;
        }
        if (!fs_under(table[i].name, olddir)) continue;
        if (fs_rejoin(table[i].name, olddir, newdir, nn, FS_NAMELEN)) {
            strlcpy(table[i].name, nn, FS_NAMELEN);
            mark_entry(&sectors,&table[i]);
        }
    }
    return commit_table(sectors) ? 0 : -1;
}

int fs_dir_count(const char *dir)
{
    int h = fs_hooked(FSH_COUNT, dir, 0, 0);
    if (h != FSH_PASS) return h;
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
        e[i].idx = (u16)i;
    }
    int moves = fs_plan(e, FS_NFILES, FS_DATA, bad, nbad);
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
    u32 used = nbad, total = FS_END - FS_DATA;
    for (int i = 0; i < FS_NFILES; i++)
        if (table[i].used) used += table[i].nsect;
    return (total - (used < total ? used : total)) / 2;
}

static int fs_bad_locked(int op, u32 lba)
{
    if (!fs_ensure()) return -1;
    int n = nbad;
    switch (op) {
    case FSB_COUNT: return n;
    case FSB_GET:   return lba < (u32)n ? bad[lba] : -1;
    case FSB_MARK:  return fb_clash(bad, n, lba, 1) >= 0 ? 0 : bad_mark(lba) ? 1 : -1;
    case FSB_CLEAR:
        if (lba) fb_del(bad, &nbad, lba);
        else nbad = 0;
        if (nbad == n) return 0;
        if (bad_flush()) return n - nbad;
        if (lba) fb_add(bad, &nbad, lba);
        else nbad = n;
    }
    return -1;
}

const char *ext_type(const char *name)
{
    const char *dot = 0;
    for (const char *p = name; *p; p++)
        if (*p == '.') dot = p;
    if (!dot || !dot[1]) return "File";
    const char *e = dot + 1;
    if (!strcasecmp(e,"fpa") || !strcasecmp(e,"pz") || !strcasecmp(e,"gz") ||
        !strcasecmp(e,"zip") || !strcasecmp(e,"tar") || !strcasecmp(e,"tgz")) return "Archive";
    if (!strcasecmp(e, "txt") || !strcasecmp(e, "md") ||
        !strcasecmp(e, "log") || !strcasecmp(e, "cfg") ||
        !strcasecmp(e, "ini")) return "Text";
    if (!strcasecmp(e, "c") || !strcasecmp(e, "h")) return "C source";
    if (!strcasecmp(e, "asm") || !strcasecmp(e, "s")) return "Assembly";
    if (!strcasecmp(e, "sh") || !strcasecmp(e, "bat") ||
        !strcasecmp(e, "cmd")) return "Script";
    if (!strcasecmp(e, "bin") || !strcasecmp(e, "img") ||
        !strcasecmp(e, "dat")) return "Binary";
    if (!strcasecmp(e, "bmp") || !strcasecmp(e, "raw") || !strcasecmp(e, "png") ||
        !strcasecmp(e, "jpg") || !strcasecmp(e, "jpeg") || !strcasecmp(e, "gif")) return "Image";
    if (!strcasecmp(e, "mid") || !strcasecmp(e, "midi")) return "Music";
    if (!strcasecmp(e, "kx")) return "KExt";
    return "File";
}

static int fs_op(int op,const char *a,const void *b,u32 n)
{
    mtx_lock(&fs_mutex);
    const char *old=debug_path(0,a);
    int r=fs_hooked(op,a,b,n);
    if(r==FSH_PASS){
        fs_media_locked();
        r=fs_hooked(op|FSH_LIVE,a,b,n);
    }
    if(r==FSH_PASS){
        switch(op){
        case FSH_READ:r=fs_read_locked(a,(u8 *)b,n);break;
        case FSH_WRITE:case FSH_REPLACE:r=fs_write_locked(a,b,n,op==FSH_REPLACE);break;
        case FSH_DELETE:r=fs_delete_locked(a);break;
        case FSH_MKDIR:r=fs_mkdir_locked(a);break;
        case FSH_TOUCH:r=fs_touch_locked(a);break;
        case FSH_RENAME:r=fs_rename_locked(a,b);break;
        default:r=fs_rename_dir_locked(a,b);
        }
        fs_hooked(op|FSH_DONE,a,b,(u32)r);
    }
    debug_done(op?2:0,old,r);
    mtx_unlock(&fs_mutex);
    return r;
}

int fs_read(const char *name,u8 *buf,u32 max){return fs_op(FSH_READ,name,buf,max);}
int fs_write(const char *name,const u8 *buf,u32 size){return fs_op(FSH_WRITE,name,buf,size);}
int fs_replace(const char *name,const u8 *buf,u32 size){return fs_op(FSH_REPLACE,name,buf,size);}
int fs_delete(const char *name){return fs_op(FSH_DELETE,name,0,0);}
int fs_mkdir(const char *name){return fs_op(FSH_MKDIR,name,0,0);}
int fs_touch(const char *name){return fs_op(FSH_TOUCH,name,0,0);}
int fs_rename(const char *oldname,const char *newname){return fs_op(FSH_RENAME,oldname,newname,0);}
int fs_rename_dir(const char *olddir,const char *newdir){return fs_op(FSH_RENAME_DIR,olddir,newdir,0);}

int fs_bad(int op,u32 lba){mtx_lock(&fs_mutex);int r=fs_bad_locked(op,lba);mtx_unlock(&fs_mutex);return r;}

int disk_write(u32 lba,const u8 *buf)
{
    u8 sec[512];
    if(lba>=FS_END||kupd_critical||!paging_span(buf,512))return -2;
    memcpy(sec,buf,512);
    mtx_lock(&fs_mutex);
    int r=fdc_write(lba,sec);
    if(lba>=FS_SUPER&&lba<FS_DATA)reload_table();
    mtx_unlock(&fs_mutex);
    return r;
}

int fs_defrag(void (*prog)(int,int)){mtx_lock(&fs_mutex);fs_media_locked();const char *old=debug_path(0,"defrag");int r=fs_defrag_locked(prog);debug_done(2,old,r);mtx_unlock(&fs_mutex);return r;}
