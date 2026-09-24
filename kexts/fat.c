/* Reads FAT volumes and writes supported FAT16 volumes on USB. */
#include "kapi.h"
#include "lfncore.inc"

static const Kapi *api;

#define kfmt            api->kfmt
#define strlen          api->strlen
#define strcmp          api->strcmp
#define strncmp         api->strncmp
#define strcasecmp      api->strcasecmp
#define strlcpy         api->strlcpy
#define memcpy          api->memcpy
#define memmove         api->memmove
#define memset          api->memset
#define human_size      api->human_size
#define human_size_kb   api->human_size_kb
#define ticks           (*api->ticks)
#define timer_alive     (*api->timer_alive)
#define rtc_read        api->rtc_read
#define rtc_now_dos     api->rtc_now_dos
#define dos_fmt         api->dos_fmt
#define usb_present     api->usb_present
#define usb_gen         api->usb_gen
#define usb_read        api->usb_read
#define usb_write       api->usb_write
#define usb_capacity_kb api->usb_capacity_kb
#define usb_capacity_sectors api->usb_capacity_sectors
#define usb_model       api->usb_model

static int  mounted;
static u32  fat_lba, data_lba;
static u32  reserved, fatsz, rootents, rootclus, root_lba;
static u32  total_clus;
static u8   spc, nfat;
static int  fattype;
static char vol_label[16];

static u8   secbuf[512];
static u32  cache_lba = 0xFFFFFFFF;

static int rd(u32 lba)
{
    if (lba == cache_lba) return 0;
    if (usb_read(lba, 1, secbuf) != 0) { cache_lba = 0xFFFFFFFF; return -1; }
    cache_lba = lba;
    return 0;
}

static u16 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static u32 seen_gen;

static u32 next_free = 2;
static u32 freec_cache;
static u8  freec_ok;
static u8  fat32_nomirror;

static void fat_hint_reset(void)
{
    next_free = 2;
    freec_ok = 0;
}

int fat_mount(void)
{
    u32 g = usb_gen();
    if (g != seen_gen) {
        seen_gen = g;
        mounted = 0;
        cache_lba = 0xFFFFFFFF;
    }
    if (mounted) return mounted > 0;
    mounted = -1;
    if (!usb_present()) return 0;

    u32 base = 0, capacity = usb_capacity_sectors(), partsecs = 0;
    if (!capacity) return 0;
    if (rd(0) != 0) return 0;

    if (secbuf[510] == 0x55 && secbuf[511] == 0xAA && rd16(secbuf+11) != 512) {
        for (int i = 0; i < 4; i++) {
            u8 *pe = secbuf + 0x1BE + i * 16;
            u8 t = pe[4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B ||
                t == 0x0C || t == 0x0E) {
                base = rd32(pe + 8);
                partsecs = rd32(pe + 12);
                if (!base || base >= capacity || !partsecs || partsecs > capacity-base)
                    return 0;
                break;
            }
        }
    }

    if (rd(base) != 0) return 0;
    u8 *b = secbuf;
    if (b[510] != 0x55 || b[511] != 0xAA || rd16(b + 11) != 512) return 0;
    spc = b[13];
    reserved = rd16(b + 14);
    nfat = b[16];
    rootents = rd16(b + 17);
    u32 totsec = rd16(b + 19);
    if (!totsec) totsec = rd32(b + 32);
    fatsz = rd16(b + 22);
    if (!fatsz) fatsz = rd32(b + 36);
    if (!spc || (spc & (spc-1)) || spc > 128 || !reserved || !nfat || !fatsz)
        return 0;

    if (!totsec || totsec > capacity-base || (partsecs && totsec > partsecs) ||
        reserved >= totsec || fatsz > (totsec-reserved)/nfat) return 0;

    fat_lba = base + reserved;
    root_lba = fat_lba + (u32)nfat * fatsz;
    u32 root_sectors = ((rootents * 32) + 511) / 512;
    if (root_sectors >= totsec-reserved-(u32)nfat*fatsz) return 0;
    data_lba = root_lba + root_sectors;

    u32 dataclusters = (totsec - (data_lba - base)) / spc;
    if (totsec <= data_lba - base || dataclusters < 1)
        return 0;

    total_clus = dataclusters + 1;
    fattype = dataclusters < 4085 ? 12 : (dataclusters < 65525 ? 16 : 32);
    rootclus = fattype == 32 ? rd32(b + 44) : 0;

    if (dataclusters > 0x0FFFFFEFu) return 0;
    u32 entries = dataclusters + 2;
    u32 fatbytes = fattype == 12 ? (entries*3+1)/2 : entries*(fattype/8);
    if (fatsz < (fatbytes+511)/512) return 0;
    if (fattype == 32 ? (rootents || rd16(b+22) || rootclus < 2 || rootclus > total_clus)
                      : (!rootents || !rd16(b+22))) return 0;

    fat32_nomirror = 0;
    if (fattype == 32) {
        u32 xf = rd16(b + 40);
        if (xf & 0x0080) {
            u32 active = xf & 0x000F;
            if (active >= nfat) return 0;
            if (active) { fat32_nomirror = 1; fat_lba += active*fatsz; }
        }
    }

    strlcpy(vol_label, "USB", sizeof vol_label);
    fat_hint_reset();

    mounted = 1;
    return 1;
}

const char *fat_label(void) { return vol_label; }

static u32 clus_lba(u32 c) { return data_lba + (c - 2) * spc; }
static int valid_cluster(u32 c)
{ return c >= 2 && c <= total_clus && c < (fattype==12 ? 0xFF0u : fattype==16 ? 0xFFF0u : 0x0FFFFFF0u); }
static u32 entry_cluster(const u8 *de)
{ return rd16(de+26) | (fattype==32 ? ((u32)(rd16(de+20)&0x0FFF)<<16) : 0); }

static u32 fat_next(u32 c)
{
    if (!valid_cluster(c)) return 0xFFFFFFFF;
    if (fattype == 16) {
        u32 off = c * 2;
        if (rd(fat_lba + off / 512) != 0) return 0xFFFFFFFF;
        u32 v = rd16(secbuf + off % 512);
        return v >= 0xFFF8 ? 0x0FFFFFFF : v;
    }
    if (fattype == 32) {
        u32 off = c * 4;
        if (rd(fat_lba + off / 512) != 0) return 0xFFFFFFFF;
        u32 v = rd32(secbuf + off % 512) & 0x0FFFFFFF;
        return v >= 0x0FFFFFF8 ? 0x0FFFFFFF : v;
    }

    u32 off = c + c / 2;
    u8 lo, hi;
    if (rd(fat_lba + off / 512) != 0) return 0xFFFFFFFF;
    lo = secbuf[off % 512];
    if (rd(fat_lba + (off + 1) / 512) != 0) return 0xFFFFFFFF;
    hi = secbuf[(off + 1) % 512];
    u32 v = (c & 1) ? ((lo >> 4) | (hi << 4)) : (lo | ((hi & 0x0F) << 8));
    return v >= 0xFF8 ? 0x0FFFFFFF : v;
}

static void fmt_name(const u8 *raw, char *out)
{
    int o = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        out[o++] = c;
    }
    if (raw[8] != ' ') {
        out[o++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) {
            char c = raw[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[o++] = c;
        }
    }
    out[o] = 0;
}

static int dir_scan(u32 dir_clus, int is_root16, int mode,
                    const char *want, FatEnt *list, int max,
                    u32 *oc, u32 *osz, int *odir, u8 *oraw)
{
    int count = 0;
    u32 clus = dir_clus;
    u32 sector = is_root16 ? root_lba : 0;
    u32 secs_left = is_root16 ? ((rootents * 32) + 511) / 512 : 0;
    u32 clus_secs = 0;

    LfnAcc lfn;
    lfn_reset(&lfn);

    u32 anchor = clus, span = 1, walked = 0;
    for (int guard = 0; guard < 100000; guard++) {
        u32 lba;
        if (is_root16) {
            if (secs_left == 0) break;
            lba = sector++;
            secs_left--;
        } else {
            if (clus_secs == 0) {
                if (clus == 0x0FFFFFFF) break;
                if (!valid_cluster(clus)) return -1;
                sector = clus_lba(clus);
                clus_secs = spc;
            }
            lba = sector++;
            clus_secs--;
        }
        if (rd(lba) != 0) return -1;
        u8 dirsec[512];
        memcpy(dirsec, secbuf, 512);

        for (int e = 0; e < 512; e += 32) {
            u8 *de = dirsec + e;
            if (de[0] == 0x00) return mode ? 0 : count;
            if (de[0] == 0xE5) { lfn_reset(&lfn); continue; }
            u8 attr = de[11];
            if (attr == 0x0F) { lfn_feed(&lfn, de); continue; }
            if (attr & 0x08) {
                lfn_reset(&lfn);
                if (mode == 0 && dir_clus == rootclus) {
                    int k = 0;
                    for (int i = 0; i < 11 && k < 15; i++)
                        if (de[i] != ' ') vol_label[k++] = de[i];
                    vol_label[k] = 0;
                }
                continue;
            }
            char nm[16];
            fmt_name(de, nm);

            char lname[64];
            int has_l = lfn_take(&lfn, de, lname, sizeof lname);
            if (!nm[0]) continue;
            if (nm[0] == '.') continue;
            u32 fc = entry_cluster(de);
            u32 sz = rd32(de + 28);
            int isdir = (attr & 0x10) != 0;

            if (mode == 1) {

                if ((has_l && !strcasecmp(lname, want)) || !strcasecmp(nm, want)) {
                    if (oc) *oc = fc;
                    if (osz) *osz = sz;
                    if (odir) *odir = isdir;
                    if (oraw) memcpy(oraw, de, 11);
                    return 1;
                }
            } else if (count < max) {
                FatEnt *fe = &list[count++];
                strlcpy(fe->name, has_l ? lname : nm, sizeof fe->name);
                fe->size = sz;
                fe->mtime = ((u32)rd16(de + 24) << 16) | rd16(de + 22);
                fe->is_dir = isdir;
            }
        }

        if (!is_root16 && clus_secs == 0) {
            clus = fat_next(clus);
            if (clus == anchor) return -1;
            if (++walked == span) { anchor=clus; span*=2; walked=0; }
        }
    }
    return mode ? 0 : count;
}

static int resolve_dir(const char *path, u32 *clus, int *root16)
{
    u32 c = (fattype == 32) ? rootclus : 0;
    int r16 = (fattype != 32);
    while (*path == '/') path++;
    while (*path) {
        char comp[64];
        int k = 0;
        while (*path && *path != '/' && k < 63) comp[k++] = *path++;
        if (*path && *path != '/') return 0;
        comp[k] = 0;
        while (*path == '/') path++;
        if (!comp[0]) break;
        u32 fc; int isdir;
        if (dir_scan(c, r16, 1, comp, 0, 0, &fc, 0, &isdir, 0) <= 0 || !isdir)
            return 0;
        c = fc;
        r16 = 0;
    }
    *clus = c;
    *root16 = r16;
    return 1;
}

int fat_list(const char *path, FatEnt *out, int max)
{
    if (!fat_mount()) return -1;
    u32 c; int r16;
    if (!resolve_dir(path, &c, &r16)) return -1;
    return dir_scan(c, r16, 0, 0, out, max, 0, 0, 0, 0);
}

int fat_read(const char *path, u8 *buf, u32 max)
{
    if (!fat_mount()) return -1;

    int slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') slash = i;
    char dir[96];
    const char *fname;
    if (slash < 0) { strlcpy(dir, "/", sizeof dir); fname = path; }
    else {
        int n = slash > 0 ? slash : 1;
        if (n >= (int)sizeof dir) return -1;
        memcpy(dir, path, n); dir[n] = 0;
        fname = path + slash + 1;
    }

    u32 c; int r16;
    if (!resolve_dir(dir, &c, &r16)) return -1;
    u32 fc, sz; int isdir;
    if (dir_scan(c, r16, 1, fname, 0, 0, &fc, &sz, &isdir, 0) <= 0 || isdir) return -1;

    u32 want = sz < max ? sz : max;
    u32 got = 0, clus = fc;
    if (want > 0x7FFFFFFFu) return -1;
    u32 visited = 0, anchor=clus, span=1, walked=0;
    while (got < want) {
        if (!valid_cluster(clus) || ++visited > total_clus-1) return -1;
        for (u32 s = 0; s < spc && got < want; s++) {
            u32 full=(want-got)/512;
            if(full){
                if(full>spc-s)full=spc-s;
                if(usb_read(clus_lba(clus)+s,full,buf+got))return -1;
                got+=full*512;s+=full-1;continue;
            }
            if (rd(clus_lba(clus) + s) != 0) return -1;
            u32 n = want - got < 512 ? want - got : 512;
            memcpy(buf + got, secbuf, n);
            got += n;
        }
        if (got < want) {
            clus = fat_next(clus);
            if (clus == anchor) return -1;
            if (++walked == span) { anchor=clus; span*=2; walked=0; }
        }
    }
    return (int)got;
}

int fat_writable(void) { return fat_mount() && fattype != 12 && !fat32_nomirror; }

static u32 fat_get(u32 c);

u32 fat_total_kb(void)
{
    if (!fat_mount()) return 0;
    return (total_clus - 1) * spc / 2;
}

u32 fat_free_kb(void)
{
    if (!fat_mount() || fattype == 12) return 0;

    if (!freec_ok) {
        u8 *scan = api->kmalloc(32768u);
        u32 batch = scan ? 64u : 1u;
        if (!scan) scan = secbuf;
        u32 freec = 0, step = fattype == 16 ? 2u : 4u;
        u32 end = total_clus + 1, limit = fattype == 16 ? 0xFFF0u : 0x0FFFFFF0u;
        if (end > limit) end = limit;
        for (u32 c = 2; c < end;) {
            u32 sector = c * step / 512;
            u32 count = (end * step + 511) / 512 - sector;
            if (count > batch) count = batch;
            if (scan == secbuf) cache_lba = 0xFFFFFFFF;
            if (usb_read(fat_lba + sector, count, scan) != 0 || usb_gen() != seen_gen) {
                if (scan != secbuf) api->kfree(scan);
                return 0;
            }
            for (u32 off = c * step % 512; off < count * 512 && c < end; off += step, c++) {
                u32 v = step == 2 ? rd16(scan + off) : rd32(scan + off) & 0x0FFFFFFF;
                if (!v) freec++;
            }
        }
        if (scan != secbuf) api->kfree(scan);
        freec_cache = freec;
        freec_ok = 1;
    }
    return freec_cache * spc / 2;
}

static int wr(u32 lba, const u8 *data)
{
    if (usb_write(lba, 1, data) != 0) { cache_lba = 0xFFFFFFFF; return -1; }
    memcpy(secbuf, data, 512);
    cache_lba = lba;
    return 0;
}

static u32 fat_get(u32 c)
{
    if (fattype == 16) {
        u32 off = c * 2;
        if (rd(fat_lba + off / 512) != 0) return 0xFFFFFFFF;
        return rd16(secbuf + off % 512);
    }
    u32 off = c * 4;
    if (rd(fat_lba + off / 512) != 0) return 0xFFFFFFFF;
    return rd32(secbuf + off % 512) & 0x0FFFFFFF;
}

static int fat_set(u32 c, u32 v)
{

    u8 sec[512];
    u32 off = fattype == 16 ? c * 2 : c * 4;
    u32 sect = off / 512, o = off % 512;
    if (rd(fat_lba + sect) != 0) return -1;
    memcpy(sec, secbuf, 512);
    if (fattype == 16) {
        sec[o] = v & 0xFF; sec[o + 1] = (v >> 8) & 0xFF;
    } else {
        u32 keep = rd32(sec + o) & 0xF0000000;
        u32 nv = keep | (v & 0x0FFFFFFF);
        sec[o] = nv; sec[o + 1] = nv >> 8; sec[o + 2] = nv >> 16; sec[o + 3] = nv >> 24;
    }

    int bad = 0;
    for (u8 f = 0; f < nfat; f++)
        if (wr(fat_lba + (u32)f * fatsz + sect, sec) != 0) {
            if (f == 0) return -1;
            bad = 1;
            break;
        }
    memcpy(secbuf, sec, 512);
    cache_lba = fat_lba + sect;
    return bad ? -1 : 0;
}

#define EOC (fattype == 32 ? 0x0FFFFFFFu : 0xFFFFu)

static void free_chain(u32 c)
{

    for (u32 guard = 0; guard <= total_clus &&
         valid_cluster(c); guard++) {
        u32 next = fat_next(c);
        if (next == 0 || next == 0xFFFFFFFF) break;

        if (fat_set(c, 0) != 0) break;
        if (freec_ok) freec_cache++;
        if (c < next_free) next_free = c;

        c = next;
    }
}

static u32 alloc_chain(int n)
{
    if (n <= 0 || total_clus < 2) return 0;
    u32 first = 0, prev = 0;
    u32 start = valid_cluster(next_free) ? next_free : 2;
    u32 c = start;
    int wrapped = 0;
    while (n > 0) {
        u32 v = fat_get(c);
        if (v == 0xFFFFFFFF) {
            if (first) free_chain(first);
            return 0;
        }
        if (v == 0) {

            if (fat_set(c, EOC) != 0) {
                if (first) free_chain(first);
                return 0;
            }
            if (freec_ok && freec_cache) freec_cache--;
            if (!first) first = c;
            else if (fat_set(prev, c) != 0) {
                fat_set(c, 0);
                if (freec_ok) freec_cache++;
                free_chain(first);
                return 0;
            }
            prev = c;
            n--;
        }
        c++;
        if (!valid_cluster(c)) {
            if (wrapped) break;
            c = 2;
            wrapped = 1;
        }
        if (wrapped && c >= start) break;
    }
    if (n > 0) { if (first) free_chain(first); return 0; }
    next_free = prev + 1;
    return first;
}

static void to_83(const char *name, u8 raw[11])
{
    memset(raw, ' ', 11);
    const char *base = name;
    for (const char *p = name; *p; p++) if (*p == '/') base = p + 1;
    const char *dot = 0;
    for (const char *p = base; *p; p++) if (*p == '.') dot = p;
    int i = 0;
    for (const char *p = base; *p && i < 8; p++) {
        if (dot && p >= dot) break;
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        raw[i++] = c;
    }
    if (dot) {
        int j = 0;
        for (const char *p = dot + 1; *p && j < 3; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 32;
            raw[8 + j++] = c;
        }
    }
}

static int dir_slot(u32 dir_clus, int is_root16, const u8 raw[11],
                    u32 *o_lba, int *o_off, u32 *oldc, int *isdir)
{
    if (isdir) *isdir = 0;
    u32 clus = dir_clus, sector = is_root16 ? root_lba : 0;
    u32 secs_left = is_root16 ? ((rootents * 32) + 511) / 512 : 0;
    u32 clus_secs = 0;
    u32 free_lba = 0; int free_off = -1;
    u32 anchor=clus, span=1, walked=0;

    for (int guard = 0; guard < 100000; guard++) {
        u32 lba;
        if (is_root16) {
            if (secs_left == 0) break;
            lba = sector++; secs_left--;
        } else {
            if (clus_secs == 0) {
                if (clus == 0x0FFFFFFF) break;
                if (!valid_cluster(clus)) return 0;
                sector = clus_lba(clus); clus_secs = spc;
            }
            lba = sector++; clus_secs--;
        }
        if (rd(lba) != 0) return 0;
        u8 dirsec[512];
        memcpy(dirsec, secbuf, 512);
        for (int e = 0; e < 512; e += 32) {
            u8 *de = dirsec + e;
            if (de[0] == 0x00 || de[0] == 0xE5) {
                if (free_off < 0) { free_lba = lba; free_off = e; }
                if (de[0] == 0x00) {
                    *o_lba = free_lba; *o_off = free_off;
                    return free_off >= 0 ? 2 : 0;
                }
                continue;
            }
            if (de[11] == 0x0F || (de[11] & 0x08)) continue;
            int match = 1;
            for (int i = 0; i < 11; i++) if (de[i] != raw[i]) { match = 0; break; }
            if (match) {
                *o_lba = lba; *o_off = e;
                if (oldc) *oldc = entry_cluster(de);
                if (isdir) *isdir = (de[11] & 0x10) ? 1 : 0;
                return 1;
            }
        }
        if (!is_root16 && clus_secs == 0) {
            clus = fat_next(clus);
            if (clus == anchor) return 0;
            if (++walked == span) { anchor=clus; span*=2; walked=0; }
        }
    }
    if (free_off >= 0) { *o_lba = free_lba; *o_off = free_off; return 2; }
    return 0;
}

static int dir_pair(u32 dir_clus, int is_root16, const u8 raw[11],
                    u32 *o_lba, int *o_off, u32 *long_lba, int *long_off,u32 *end_lba)
{
    (void)raw;int ended=0;*end_lba=0;
    u32 clus = dir_clus, sector = is_root16 ? root_lba : 0;
    u32 secs_left = is_root16 ? ((rootents * 32) + 511) / 512 : 0;
    u32 clus_secs = 0;
    u32 free_lba = 0; int free_off = -1;
    u32 anchor=clus, span=1, walked=0;

    for (int guard = 0; guard < 100000; guard++) {
        u32 lba;
        if (is_root16) {
            if (secs_left == 0) break;
            lba = sector++; secs_left--;
        } else {
            if (clus_secs == 0) {
                if (clus == 0x0FFFFFFF) break;
                if (!valid_cluster(clus)) return 0;
                sector = clus_lba(clus); clus_secs = spc;
            }
            lba = sector++; clus_secs--;
        }
        if (rd(lba) != 0) return 0;
        u8 dirsec[512];
        memcpy(dirsec, secbuf, 512);
        for (int e = 0; e < 512; e += 32) {
            u8 *de = dirsec + e;
            if(de[0]==0)ended=1;
            if(ended||de[0]==0xE5){
                if(free_off>=0){
                    if(ended){
                        if(e<480)*end_lba=lba;
                        else if(is_root16){if(secs_left)*end_lba=sector;}
                        else if(clus_secs)*end_lba=sector;
                        else{u32 next=fat_next(clus);if(valid_cluster(next))*end_lba=clus_lba(next);else if(next!=0x0fffffffu)return 0;}
                    }
                    *long_lba=free_lba;*long_off=free_off;*o_lba=lba;*o_off=e;return 2;
                }
                free_lba=lba;free_off=e;
            }else free_off=-1;

        }
        if (!is_root16 && clus_secs == 0) {
            clus = fat_next(clus);
            if (clus == anchor) return 0;
            if (++walked == span) { anchor=clus; span*=2; walked=0; }
        }
    }
    return 0;
}

static void split_path(const char *path, char *dir, int dcap, const char **fname)
{
    int slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') slash = i;
    if (slash < 0) { strlcpy(dir, "/", dcap); *fname = path; return; }
    int n = slash > 0 ? slash : 1;
    if (n > dcap - 1) { dir[0]=0; *fname=""; return; }
    memcpy(dir, path, n); dir[n] = 0;
    *fname = path + slash + 1;
}

u8 fat_dbg_step;

int fat_write(const char *path, const u8 *buf, u32 size)
{
    fat_dbg_step = 0;
    if (!fat_writable()) return -1;
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;

    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 raw[11];

    int lookup = dir_scan(dclus, r16, 1, fname, 0, 0, 0, 0, 0, raw);
    if (lookup < 0) return -1;
    if (!lookup)
        to_83(fname, raw);

    u32 slot_lba; int slot_off; u32 oldc = 0; int wasdir = 0;
    int found = dir_slot(dclus, r16, raw, &slot_lba, &slot_off, &oldc, &wasdir);
    if (found == 0) return -2;
    if (found == 1 && !lookup) return -1;

    if (found == 1 && wasdir) return -3;
    int longname=0;u32 long_lba=0,end_lba=0;int long_off=0;
    if(!lookup&&strlen(fname)<=13){
        const char *dot=0;for(const char *p=fname;*p;p++)if(*p=='.')dot=p;
        longname=dot?dot-fname>8||strlen(dot+1)>3:strlen(fname)>8;
        if(longname){
            if(found==1)return -1;
            if(dir_pair(dclus,r16,raw,&slot_lba,&slot_off,&long_lba,&long_off,&end_lba)!=2)return -2;
        }
    }
    fat_dbg_step = 1;

    u32 bytespc = (u32)spc * 512;
    u32 count = size/bytespc + (size%bytespc != 0);
    if (count > total_clus-1) return -2;
    int nclus = (int)count;
    u32 first = alloc_chain(nclus);
    if (nclus && !first) return -2;
    fat_dbg_step = 2;

    u32 off = 0, c = first;
    while (off < size) {
        if (!valid_cluster(c)) goto fail;
        for (u32 s = 0; s < spc && off < size; s++) {
            u8 sec[512];
            memset(sec, 0, 512);
            u32 n = size - off < 512 ? size - off : 512;
            memcpy(sec, buf + off, n);
            if (wr(clus_lba(c) + s, sec) != 0) goto fail;
            off += n;
        }
        if (off < size) c = fat_next(c);
    }
    fat_dbg_step = 3;

    if (rd(slot_lba) != 0) goto fail;
    fat_dbg_step = 4;
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    u8 *de = dsec + slot_off;
    memset(de, 0, 32);
    memcpy(de, raw, 11);
    de[11] = 0x20;
    u32 dt = rtc_now_dos();
    de[22] = dt & 0xFF; de[23] = (dt >> 8) & 0xFF;
    de[24] = (dt >> 16) & 0xFF; de[25] = (dt >> 24) & 0xFF;
    de[26] = first & 0xFF; de[27] = (first >> 8) & 0xFF;
    de[20] = (first >> 16) & 0xFF; de[21] = (first >> 24) & 0xFF;
    de[28] = size; de[29] = size >> 8; de[30] = size >> 16; de[31] = size >> 24;

    if(longname){
        if(end_lba==slot_lba)dsec[slot_off+32]=0;
        else if(end_lba){u8 tail[512];if(rd(end_lba))goto fail;memcpy(tail,secbuf,512);tail[0]=0;if(wr(end_lba,tail))goto fail;}
        u8 entry[32]={0};static const u8 pos[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
        entry[0]=0x41;entry[11]=15;entry[13]=lfn_checksum(raw);
        u32 len=strlen(fname);
        for(u32 i=0;i<13;i++){u16 ch=i<len?(u8)fname[i]:i==len?0:0xffff;entry[pos[i]]=ch;entry[pos[i]+1]=ch>>8;}
        if(long_lba==slot_lba)memcpy(dsec+long_off,entry,32);
        else{
            u8 lsec[512];if(rd(long_lba))goto fail;memcpy(lsec,secbuf,512);memcpy(lsec+long_off,entry,32);
            if(wr(long_lba,lsec))goto fail;
        }
    }
    if (wr(slot_lba, dsec) != 0) return -1;

    if (found == 1 && oldc >= 2) free_chain(oldc);
    fat_dbg_step = 5;
    return 0;

fail:

    if (first >= 2) free_chain(first);
    return -1;
}

int fat_append(const char *path, const u8 *buf, u32 size)
{
    if (!fat_writable()) return -1;
    if (!size) return 0;
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;

    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 raw[11];
    int lookup = dir_scan(dclus, r16, 1, fname, 0, 0, 0, 0, 0, raw);
    if (lookup < 0) return -1;
    if (!lookup) return fat_write(path, buf, size);

    u32 slot_lba; int slot_off; u32 first = 0; int wasdir = 0;
    int found = dir_slot(dclus, r16, raw, &slot_lba, &slot_off, &first, &wasdir);
    if (found == 0) return -2;
    if (found == 1 && wasdir) return -3;
    if (found != 1) return fat_write(path, buf, size);

    if (rd(slot_lba) != 0) return -1;
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    u8 *de = dsec + slot_off;
    u32 cur = (u32)de[28] | ((u32)de[29] << 8) |
              ((u32)de[30] << 16) | ((u32)de[31] << 24);

    u32 bytespc = (u32)spc * 512;
    if (size > 0xFFFFFFFFu-cur) return -1;
    u32 have = 0;

    u32 tail = 0;
    if (first) {

        u32 c = first;
        u32 anchor=c, span=1, walked=0;
        while (c != 0x0FFFFFFFu) {
            if (!valid_cluster(c) || ++have > total_clus-1) return -1;
            tail = c;
            c = fat_next(c);
            if (c == anchor) return -1;
            if (++walked == span) { anchor=c; span*=2; walked=0; }
        }
    }
    if (cur/bytespc + (cur%bytespc != 0) > have) return -1;
    u32 need = (cur+size)/bytespc + ((cur+size)%bytespc != 0);
    if (need > total_clus-1) return -2;
    if (need > have) {
        u32 add = alloc_chain((int)(need - have));
        if (!add) return -2;
        if (first < 2) first = add;
        else {

            if (fat_set(tail, add) != 0) return -1;
        }
    }

    u32 pos = cur, done = 0;
    u32 c = first;
    for (u32 k = 0, ci = pos / bytespc; k < ci; k++) {
        if (!valid_cluster(c)) return -1;
        c = fat_next(c);
    }
    while (done < size) {
        if (!valid_cluster(c)) return -1;
        u32 inclus = pos % bytespc;
        u32 s = inclus / 512, soff = inclus % 512;
        for (; s < spc && done < size; s++) {
            u32 lba = clus_lba(c) + s;
            u32 full=soff?0:(size-done)/512;
            if(full){
                if(full>spc-s)full=spc-s;
                cache_lba=0xFFFFFFFF;
                if(usb_write(lba,full,buf+done))return -1;
                done+=full*512;pos+=full*512;s+=full-1;continue;
            }
            u8 sec[512];
            memset(sec, 0, 512);
            if (soff) {
                if (rd(lba) != 0) return -1;
                memcpy(sec, secbuf, 512);
            }
            u32 n = 512 - soff;
            if (n > size - done) n = size - done;
            memcpy(sec + soff, buf + done, n);
            if (wr(lba, sec) != 0) return -1;
            done += n; pos += n; soff = 0;
        }
        if (done < size) c = fat_next(c);
    }
    if (done < size) return -1;

    u32 nsz = cur + size;
    de[26] = first & 0xFF;        de[27] = (first >> 8) & 0xFF;
    de[20] = (first >> 16) & 0xFF; de[21] = (first >> 24) & 0xFF;
    de[28] = nsz; de[29] = nsz >> 8; de[30] = nsz >> 16; de[31] = nsz >> 24;
    u32 dt = rtc_now_dos();
    de[22] = dt & 0xFF; de[23] = (dt >> 8) & 0xFF;
    de[24] = (dt >> 16) & 0xFF; de[25] = (dt >> 24) & 0xFF;
    if (wr(slot_lba, dsec) != 0) return -1;
    return 0;
}

static void dirent_dir(u8 *de, const u8 raw[11], u32 clus, u32 dt)
{
    memset(de, 0, 32);
    memcpy(de, raw, 11);
    de[11] = 0x10;
    de[22] = dt & 0xFF;         de[23] = (dt >> 8) & 0xFF;
    de[24] = (dt >> 16) & 0xFF; de[25] = (dt >> 24) & 0xFF;
    de[26] = clus & 0xFF;       de[27] = (clus >> 8) & 0xFF;
    de[20] = (clus >> 16) & 0xFF; de[21] = (clus >> 24) & 0xFF;

}

int fat_mkdir(const char *path)
{
    if (!fat_writable()) return -1;
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;

    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 raw[11];
    to_83(fname, raw);

    u32 slot_lba; int slot_off; u32 oldc = 0;
    int found = dir_slot(dclus, r16, raw, &slot_lba, &slot_off, &oldc, 0);
    if (found == 1) return -1;
    if (found == 0) return -2;

    u32 first = alloc_chain(1);
    if (!first) return -2;

    u8 sec[512];
    memset(sec, 0, 512);
    for (u32 s = 0; s < spc; s++)
        if (wr(clus_lba(first) + s, sec) != 0) { free_chain(first); return -1; }

    u32 dt = rtc_now_dos();
    u32 parent = (r16 || dclus == rootclus) ? 0 : dclus;
    u8 dot[11], dotdot[11];
    memset(dot, ' ', 11);    dot[0] = '.';
    memset(dotdot, ' ', 11); dotdot[0] = '.'; dotdot[1] = '.';
    dirent_dir(sec, dot, first, dt);
    dirent_dir(sec + 32, dotdot, parent, dt);
    if (wr(clus_lba(first), sec) != 0) { free_chain(first); return -1; }

    if (rd(slot_lba) != 0) { free_chain(first); return -1; }
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    dirent_dir(dsec + slot_off, raw, first, dt);

    if (wr(slot_lba, dsec) != 0) return -1;
    return 0;
}

int fat_rename(const char *path, const char *newname)
{
    if (!fat_writable()) return -1;
    if (!newname || !newname[0]) return -1;
    int namelen = strlen(newname);
    if (newname[0] == ' ' || newname[0] == '.' ||
        newname[namelen-1] == ' ' || newname[namelen-1] == '.') return -1;
    for (const char *p=newname; *p; p++)
        if ((u8)*p < 32 || *p=='/' || *p=='\\' || *p==':' || *p=='*' ||
            *p=='?' || *p=='"' || *p=='<' || *p=='>' || *p=='|') return -1;
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;

    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 oldraw[11], newraw[11];
    if (dir_scan(dclus, r16, 1, fname, 0, 0, 0, 0, 0, oldraw) <= 0) return -1;
    if (!strcasecmp(fname,newname)) return 0;
    int taken = dir_scan(dclus, r16, 1, newname, 0, 0, 0, 0, 0, newraw);
    if (taken < 0) return -1;
    if (taken && strcasecmp(fname,newname)) return -1;
    to_83(newname, newraw);

    int same = 1;
    for (int i = 0; i < 11; i++) if (oldraw[i] != newraw[i]) { same = 0; break; }
    if (same) return 0;

    u32 lba; int off; u32 c = 0;
    if (dir_slot(dclus, r16, newraw, &lba, &off, &c, 0) == 1) return -1;
    if (dir_slot(dclus, r16, oldraw, &lba, &off, &c, 0) != 1) return -1;
    if (rd(lba) != 0) return -1;
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    memcpy(dsec + off, newraw, 11);
    return wr(lba, dsec) == 0 ? 0 : -1;
}

static int entry_info(const char *path, u32 *slot_lba, int *slot_off,
                      u32 *clus, int *isdir)
{
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;
    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 raw[11];
    int lookup = dir_scan(dclus, r16, 1, fname, 0, 0, 0, 0, 0, raw);
    if (lookup <= 0) return -1;
    return dir_slot(dclus, r16, raw, slot_lba, slot_off, clus, isdir) == 1
           ? 0 : -1;
}

int fat_dir_empty(u32 clus)
{
    if (!valid_cluster(clus)) return 0;
    u32 anchor=clus, span=1, walked=0;
    for (int guard = 0; guard < 100000; guard++) {
        if (clus == 0x0FFFFFFF) return 1;
        if (!valid_cluster(clus)) return 0;
        for (u32 s = 0; s < spc; s++) {
            if (rd(clus_lba(clus) + s) != 0) return 0;
            u8 sec[512];
            memcpy(sec, secbuf, 512);
            for (int e = 0; e < 512; e += 32) {
                u8 *de = sec + e;
                if (de[0] == 0x00) return 1;
                if (de[0] == 0xE5) continue;
                if (de[11] == 0x0F) continue;
                if (de[0] == '.' && (de[1] == ' ' ||
                    (de[1] == '.' && de[2] == ' '))) continue;
                return 0;
            }
        }
        clus = fat_next(clus);
        if (clus == anchor) return 0;
        if (++walked == span) { anchor=clus; span*=2; walked=0; }
    }
    return 0;
}

int fat_rmdir(const char *path)
{
    if (!fat_writable()) return -1;
    u32 slot_lba; int slot_off; u32 clus = 0; int isdir = 0;
    if (entry_info(path, &slot_lba, &slot_off, &clus, &isdir) != 0) return -1;
    if (!isdir) return -1;
    if (!fat_dir_empty(clus)) return -3;
    if (rd(slot_lba) != 0) return -1;
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    dsec[slot_off] = 0xE5;
    if (wr(slot_lba, dsec) != 0) return -1;
    if (clus >= 2) free_chain(clus);
    return 0;
}

int fat_delete(const char *path)
{
    if (!fat_writable()) return -1;
    char dir[96]; const char *fname;
    split_path(path, dir, sizeof dir, &fname);
    if (!fname[0]) return -1;
    u32 dclus; int r16;
    if (!resolve_dir(dir, &dclus, &r16)) return -1;
    u8 raw[11];
    int lookup = dir_scan(dclus, r16, 1, fname, 0, 0, 0, 0, 0, raw);
    if (lookup <= 0) return -1;
    u32 slot_lba; int slot_off; u32 oldc = 0; int isdir = 0;
    if (dir_slot(dclus, r16, raw, &slot_lba, &slot_off, &oldc, &isdir) != 1)
        return -1;

    if (isdir) return -1;
    if (rd(slot_lba) != 0) return -1;
    u8 dsec[512];
    memcpy(dsec, secbuf, 512);
    dsec[slot_off] = 0xE5;

    u8 want = lfn_checksum(raw);
    for (int e = slot_off - 32; e >= 0; e -= 32) {
        u8 *pd = dsec + e;
        if (pd[11] != 0x0F || pd[13] != want) break;
        pd[0] = 0xE5;
    }

    if (wr(slot_lba, dsec) != 0) return -1;
    if (oldc >= 2) free_chain(oldc);
    return 0;
}

int fat_exists(const char *path)
{
    if (!fat_mount()) return 0;
    u32 lba; int off; u32 clus = 0; int isdir = 0;
    if (entry_info(path, &lba, &off, &clus, &isdir) != 0) return 0;
    return isdir ? 2 : 1;
}

static const FatOps fat_ops = {
    .mount    = fat_mount,
    .list     = fat_list,
    .read     = fat_read,
    .write    = fat_write,
    .append   = fat_append,
    .del      = fat_delete,
    .writable = fat_writable,
    .label    = fat_label,
    .total_kb = fat_total_kb,
    .free_kb  = fat_free_kb,
    .abi      = FAT_ABI,
    .mkdir    = fat_mkdir,
    .rename   = fat_rename,
    .rmdir    = fat_rmdir,
    .exists   = fat_exists,
};

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "FAT"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    api->register_fat(&fat_ops);
    return 0;
}
