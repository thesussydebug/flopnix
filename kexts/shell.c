/* Draws terminal windows and sends commands to the kernel shell. */
#include "kapi.h"
#include "shpath.h"
#include "net_wire.inc"
#include "ramtest.inc"
#include "shcwd.inc"
#include "shspec.inc"
#include "tree.inc"
#include "fspath.inc"
#include "fdchealth.inc"
#include "ntpcore.inc"
#include "lz.inc"
#include "diskmap.inc"

static const Kapi *api;

static int rt_usable(u32 base)
{
    if (!api->mem_mapped) return 1;
    for (u32 off = 0; off < 0x10000; off += 0x1000)
        if (!api->mem_mapped(base + off)) return 0;
    return 1;
}

#define ticks           (*api->ticks)
#define timer_alive     (*api->timer_alive)

#define OS_NAME    "FLOPNIX"
#define OS_VER     (api->os_version)

static void tputc(char c) { char s[2] = { c, 0 }; api->shell_print(s); }
static void tprint(const char *s) { api->shell_print(s); }

#define kfmt            api->kfmt
#define strcmp          api->strcmp
#define strncmp         api->strncmp
#define strcasecmp      api->strcasecmp
#define strlen          api->strlen
#define strlcpy         api->strlcpy
#define memcpy          api->memcpy
#define memmove         api->memmove
#define memset          api->memset
#define human_size      api->human_size
#define human_size_kb   api->human_size_kb
#define outb            api->outb
#define inb             api->inb
#define outw            api->outw
#define inw             api->inw
#define outl            api->outl
#define inl             api->inl
#define fs_read         api->fs_read
#define fs_write        api->fs_write
#define fs_delete       api->fs_delete
#define fs_touch        api->fs_touch
#define fs_exists       api->fs_exists
#define fs_slot         api->fs_slot
#define fs_free_kb      api->fs_free_kb
#define fs_defrag       api->fs_defrag
#define fs_ensure()     (fs_slot(0) != 0)
#define fdc_read        api->disk_read
#define net_up          api->net_up
#define net_dhcp        api->net_dhcp
#define net_ping        api->net_ping
#define net_dns         api->net_dns
#define net_http_get    api->net_http_get
#define net_get         api->net_get
#define net_set         api->net_set
#define net_parse_ip    api->net_parse_ip
#define net_mac_get     api->net_mac
#define config_save     api->config_save
#define reboot          api->reboot
#define kernel_update   api->kernel_update
#define klog_read       api->klog_read
#define rtc_read        api->rtc_read
#define rtc_now_dos     api->rtc_now_dos
#define cpu_brand       api->cpu_brand
#define cpu_mhz         api->cpu_mhz
#define gui_pump        api->gui_pump
#define ext_type        api->ext_type
#define usb_present     api->usb_present
#define usb_capacity_kb api->usb_capacity_kb
#define usb_model       api->usb_model
#define fat_list        api->fat_list
#define fat_read        api->fat_read
#define fat_write       api->fat_write
#define fat_delete      api->fat_delete
#define fat_free_kb     api->fat_free_kb
#define fat_writable    api->fat_writable
#define fat_label       api->fat_label
#define fat_mount       api->fat_mount
#define opener_dispatch api->open_with
#define fat_total_kb    api->fat_total_kb
#define dos_fmt         api->dos_fmt
#define kext_count      api->kext_count
#define kext_get        api->kext_get
#define kext_load       api->kext_load
#define fill_rect       api->fill_rect
#define draw_text       api->draw_text
#define draw_text_scaled api->draw_text_scaled
#define flip            api->present
#define win_slot        api->win_slot
#define win_max         api->win_max
#define win_open        api->win_open
#define app_find        api->app_find
#define cmd_usage       api->cmd_usage
#define used_kb         api->mem_used_kb
#define iobuf           api->iobuf
#define IOBUF_SZ        ((int)api->iobuf_size)
#define CFG             (api->cfg)
#define SW              ((int)api->boot_info(BI_SCREEN_W))
#define SH              ((int)api->boot_info(BI_SCREEN_H))

static void resolve(const char *in, char *out, int cap);

static void os_release(void)
{
    char r[80];
    kfmt(r, sizeof r, "flopnix %s #1 i386 (built %s)",
         api->os_version, api->os_build_date);
    tprint(r);
}

static FatEnt fe_scratch[128];

static void defrag_prog(int done, int total)
{
    (void)done; (void)total;
    gui_pump();
}

static u8  wget_buf[48 * 1024];
static int wget_n, wget_tofile, wget_over;

static u32 kupd_n;
static int kupd_over;
static int kupd_sink(const u8 *chunk, int len, void *ctx)
{
    (void)ctx;
    if (kupd_n + (u32)len > (u32)IOBUF_SZ) { kupd_over = 1; return 0; }
    memcpy(iobuf + kupd_n, chunk, len);
    kupd_n += (u32)len;
    return 1;
}

static int wget_sink(const u8 *chunk, int len, void *ctx)
{
    (void)ctx;
    if (wget_tofile) {
        int room = (int)sizeof wget_buf - wget_n;
        int take = len < room ? len : room;
        for (int i = 0; i < take; i++) wget_buf[wget_n++] = chunk[i];
        if (take < len) { wget_over = 1; return 0; }
    } else {
        for (int i = 0; i < len; i++) tputc((char)chunk[i]);
    }
    return 1;
}

static void do_wget(const char *args)
{
    char url[128], host[64], path[96], save[64];
    while (*args == ' ') args++;
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof url - 1) { url[i] = args[i]; i++; }
    url[i] = 0;
    const char *rest = args + i;
    while (*rest == ' ') rest++;
    int have_save = 0, sn = 0;
    while (*rest && *rest != ' ' && sn < (int)sizeof save - 1) { save[sn++] = *rest++; have_save = 1; }
    save[sn] = 0;

    u16 port;
    if (!nw_url_parse(url, host, sizeof host, &port, path, sizeof path)) {
        tprint("usage: wget http://host[:port]/path [savefile]\n");
        return;
    }
    if (!net_up()) { tprint("wget: no network device\n"); return; }

    char msg[128];
    u32 ip = net_dns(host, 300);
    if (!ip) { kfmt(msg, sizeof msg, "wget: cannot resolve %s\n", host); tprint(msg); return; }
    kfmt(msg, sizeof msg, "connecting to %s (%d.%d.%d.%d:%d)...\n", host,
         ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24, port);
    tprint(msg);

    wget_n = 0; wget_over = 0; wget_tofile = have_save;
    int st = net_http_get(ip, port, host, path, wget_sink, 0, 500);
    if (st == -2)      { tprint("wget: no network device\n"); return; }
    if (st == -3)      { tprint("wget: cannot reach that address\n"); return; }
    if (st == -4)      { tprint("\nwget: download cut short - not saved\n"); return; }
    if (st == -1)      { tprint("wget: no response (timed out)\n"); return; }
    if (wget_over)     { tprint("\nwget: response too large for the buffer\n"); return; }

    if (have_save) {
        char norm[64];
        resolve(save, norm, sizeof norm);
        int w = fs_write(norm, wget_buf, (u32)wget_n);
        if (w == -2)      tprint("wget: disk full\n");
        else if (w < 0)   tprint("wget: write error\n");
        else { kfmt(msg, sizeof msg, "HTTP %d - saved %d bytes to %s\n", st, wget_n, norm); tprint(msg); }
    } else {
        kfmt(msg, sizeof msg, "\n-- HTTP %d --\n", st);
        tprint(msg);
    }
}

static void do_fetch(void)
{
    char b[64], r[40];
    u32 s = ticks / 100;
    static const char *logo[8] = {
        " ______ ", "| |__| |", "|  __  |", "| |  | |",
        "|_|__|_|", "        ", "        ", "        "
    };
    for (int i = 0; i < 8; i++) {
        switch (i) {
        case 0: kfmt(r, sizeof r, "root@flopnix"); break;
        case 1: kfmt(r, sizeof r, "os:      %s %s", OS_NAME, OS_VER); break;
        case 2: kfmt(r, sizeof r, "kernel:  flopnix %s i386", OS_VER); break;
        case 3: kfmt(r, sizeof r, "uptime:  %u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60); break;
        case 4: kfmt(r, sizeof r, "memory:  %u MB", api->boot_info(BI_MEM_KB) / 1024); break;
        case 5: kfmt(r, sizeof r, "display: %dx%dx8 %s", SW, SH,
                     api->boot_info(BI_VBE) == 2 ? "(VESA banked)" :
                     api->boot_info(BI_VBE)     ? "(VESA)" : "(VGA)"); break;
        case 6:
            if (net_up()) kfmt(r, sizeof r, "net:     %d.%d.%d.%d %s",
                net_get(NET_IP) & 0xFF, (net_get(NET_IP) >> 8) & 0xFF,
                (net_get(NET_IP) >> 16) & 0xFF, net_get(NET_IP) >> 24,
                net_get(NET_DHCP_OK) ? "(DHCP)" : "(static)");
            else kfmt(r, sizeof r, "net:     no device");
            break;
        default:
            if (usb_present()) {
                char hs[16];
                human_size_kb(usb_capacity_kb(), hs, sizeof hs);
                kfmt(r, sizeof r, "usb:     %s", hs);
            } else kfmt(r, sizeof r, "usb:     none");
        }
        kfmt(b, sizeof b, "%s  %s\n", logo[i], r);
        tprint(b);
    }
}

#include "shcmd.inc"

static const char *calc_err;
static i32 calc_expr(const char **pp);

static i32 calc_factor(const char **pp)
{
    const char *p = *pp;
    while (*p == ' ') p++;
    if (*p == '(') {
        p++;
        i32 v = calc_expr(&p);
        while (*p == ' ') p++;
        if (*p == ')') p++; else calc_err = "missing )";
        *pp = p;
        return v;
    }
    if (*p == '-') { p++; *pp = p; return -calc_factor(pp); }
    if (*p < '0' || *p > '9') { calc_err = "bad expression"; *pp = p; return 0; }
    i32 v = 0;
    if (p[0] == '0' && (p[1] | 32) == 'x') {
        p += 2;
        while ((*p >= '0' && *p <= '9') || ((*p | 32) >= 'a' && (*p | 32) <= 'f')) {
            v = v * 16 + (*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10);
            p++;
        }
    } else while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *pp = p;
    return v;
}

static i32 calc_term(const char **pp)
{
    i32 v = calc_factor(pp);
    for (;;) {
        const char *p = *pp;
        while (*p == ' ') p++;
        if (*p == '*') { p++; *pp = p; v *= calc_factor(pp); }
        else if (*p == '/' || *p == '%') {
            char op = *p++;
            *pp = p;
            i32 d = calc_factor(pp);
            if (d == 0) { calc_err = "divide by zero"; return 0; }
            v = op == '/' ? v / d : v % d;
        } else { *pp = p; return v; }
    }
}

static i32 calc_expr(const char **pp)
{
    i32 v = calc_term(pp);
    for (;;) {
        const char *p = *pp;
        while (*p == ' ') p++;
        if (*p == '+') { p++; *pp = p; v += calc_term(pp); }
        else if (*p == '-') { p++; *pp = p; v -= calc_term(pp); }
        else { *pp = p; return v; }
    }
}

static void resolve(const char *in, char *out, int cap)
{
    char raw[128];int drive=0;
    const char *cwd = api->shell_cwd ? api->shell_cwd() : "";
    if(sp_token(&in,raw,sizeof raw)<0||*in||!sp_resolve(cwd,raw,&drive,out,cap)||drive){
        out[0]=0;if(drive)tprint("This command requires an A: path. Use open, cat, ls or the USB commands for U:.\n");
    }
}
static int path_arg(const char *args,int *drive,char *path,int cap)
{
    char raw[128];int n=sp_token(&args,raw,sizeof raw);
    return n>=0&&!*args&&sp_resolve(api->shell_cwd(),raw,drive,path,cap);
}
static int path_info(int drive,const char *path,u32 *size,int *is_dir)
{
    if(!drive){
        if(!path[0]||api->fs_is_dir(path)){*size=0;*is_dir=1;return 1;}
        for(int i=0;i<FS_NFILES;i++){const FsEnt *e=fs_slot(i);if(e&&e->used&&!strcmp(e->name,path)){*size=e->size;*is_dir=0;return 1;}}
        return 0;
    }
    if(!strcmp(path,"/")){*size=0;*is_dir=1;return 1;}
    char parent[96];strlcpy(parent,path,sizeof parent);char *leaf=parent;
    for(char *p=parent;*p;p++)if(*p=='/')leaf=p+1;
    char name[64];strlcpy(name,leaf,sizeof name);
    if(leaf==parent+1)parent[1]=0;else if(leaf>parent)leaf[-1]=0;else strlcpy(parent,"/",sizeof parent);
    int n=fat_list(parent,fe_scratch,128);
    for(int i=0;i<n;i++)if(!strcasecmp(fe_scratch[i].name,name)){*size=fe_scratch[i].size;*is_dir=fe_scratch[i].is_dir;return 1;}
    return 0;
}
static int path_read(int drive,const char *path,u8 *buffer,int cap)
{
    u32 size=0;int dir=0;
    if(!path_info(drive,path,&size,&dir))return -1;
    if(dir)return -3;if(size>(u32)cap)return -4;
    int n=drive?fat_read(path,buffer,cap):fs_read(path,buffer,cap);
    return n>=0&&(u32)n!=size?FS_EIO:n;
}
static void copy_move(const char *args,int move)
{
    char raw[128],src[96],dst[96];int sd=0,dd=0;
    if(sp_token(&args,raw,sizeof raw)<=0||!sp_resolve(api->shell_cwd(),raw,&sd,src,sizeof src)||
       sp_token(&args,raw,sizeof raw)<=0||*args||!sp_resolve(api->shell_cwd(),raw,&dd,dst,sizeof dst)){
        tprint("Use two paths; put names containing spaces in quotes.\n");return;
    }
    u32 size;int dir;
    if(path_info(dd,dst,&size,&dir)&&dir){
        const char *leaf=src;for(const char *p=src;*p;p++)if(*p=='/')leaf=p+1;
        int n=strlen(dst),m=strlen(leaf),slash=n&&dst[n-1]!='/';
        if(n+slash+m>=(!dd?FS_NAMELEN:(int)sizeof dst)){tprint("Destination path is too long.\n");return;}
        if(slash)dst[n++]='/';strlcpy(dst+n,leaf,sizeof dst-n);
    }
    if(sd==dd&&!(sd?strcasecmp(src,dst):strcmp(src,dst))){tprint("Source and destination are the same file.\n");return;}
    if(path_info(dd,dst,&size,&dir)){tprint("Destination already exists. Choose another name.\n");return;}
    int n=path_read(sd,src,iobuf,IOBUF_SZ);
    if(n<0){tprint(n==-4?"File exceeds the available transfer buffer.\n":n==-3?"Use Files to copy folders.\n":"Unable to read the source file.\n");return;}
    int r=dd?fat_write(dst,iobuf,n):fs_write(dst,iobuf,n);
    if(r){tprint("Unable to write the destination; the source was kept.\n");return;}
    if(move&&(sd?fat_delete(src):fs_delete(src)))tprint("Copied, but unable to remove the source.\n");
    api->broadcast("fs.changed","");
}
static void open_path(const char *args,int create)
{
    while(*args){
        char raw[128],path[96];int drive=0;
        if(sp_token(&args,raw,sizeof raw)<=0||!sp_resolve(api->shell_cwd(),raw,&drive,path,sizeof path)){
            tprint("Invalid path. Use quotes around names containing spaces.\n");return;
        }
        const char *leaf=path;for(const char *p=path;*p;p++)if(*p=='/')leaf=p+1;
        int len=strlen(leaf);
        if(len>3&&!strcasecmp(leaf+len-3,".kx")){
            if(drive)tprint("Copy the extension to A: before loading it.\n");
            else if(opener_dispatch(path,0,0,0))tprint("Could not open that extension.\n");
            continue;
        }
        if(create&&!drive&&!fs_exists(path)&&fs_write(path,(const u8 *)"",0)){tprint("Could not create the file.\n");continue;}
        int n=path_read(drive,path,iobuf,IOBUF_SZ);
        if(n<0){tprint("Could not read the file.\n");continue;}
        if(opener_dispatch(drive?leaf:path,drive?path:0,iobuf,n))tprint("No compatible app could open the file.\n");
    }
}

static int under(const char *name, const char *dir)
{
    int i = 0;
    while (dir[i]) {
        if (name[i] != dir[i]) return 0;
        i++;
    }
    return name[i] == '/';
}

static int dir_exists(const char *dir)
{
    if (!dir[0]) return 1;
    if (!fs_ensure()) return 0;
    for (int i = 0; i < FS_NFILES; i++) {
        FsEnt *e = fs_slot(i);
        if (!e->used) continue;
        if (!strcmp(e->name, dir)) return 1;
        if (under(e->name, dir)) return 1;
    }
    return 0;
}

static int child_dirs(const char *dir, char out[][SC_MAX], int max)
{
    int n = 0, dl = (int)strlen(dir);
    for (int i = 0; i < FS_NFILES; i++) {
        FsEnt *e = fs_slot(i);
        if (!e->used) continue;
        if (dir[0] && !under(e->name, dir)) continue;
        const char *rest = dir[0] ? e->name + dl + 1 : e->name;
        int k = 0;
        while (rest[k] && rest[k] != '/') k++;

        if (!rest[k] && !(e->attr & FS_ATTR_DIR)) continue;
        char nm[SC_MAX];
        int m = k < SC_MAX - 1 ? k : SC_MAX - 1;
        for (int j = 0; j < m; j++) nm[j] = rest[j];
        nm[m] = 0;
        int dup = 0;
        for (int j = 0; j < n; j++) if (!strcmp(out[j], nm)) { dup = 1; break; }
        if (!dup && n < max) strlcpy(out[n++], nm, SC_MAX);
    }
    return n;
}

static int child_files(const char *dir, int *idx, int max)
{
    int n = 0;
    for (int i = 0; i < FS_NFILES && n < max; i++) {
        FsEnt *e = fs_slot(i);
        if (!e->used || (e->attr & FS_ATTR_DIR)) continue;

        char d[SC_MAX];
        sc_parent(e->name, d, sizeof d);
        if (strcmp(d, dir)) continue;
        idx[n++] = i;
    }
    return n;
}

static void tree_level(const char *dir, unsigned char *flags, int depth,
                       int *nfile, int *ndir)
{
    if (depth >= 7) return;
    char subs[16][SC_MAX];
    int fidx[FS_NFILES];
    int nd = child_dirs(dir, subs, 16);
    int nf = child_files(dir, fidx, FS_NFILES);
    int left = nd + nf;

    for (int i = 0; i < nd; i++) {
        left--;
        char pre[40], line[96];
        tr_prefix(flags, depth, left == 0, pre, sizeof pre);
        kfmt(line, sizeof line, "%s%s/\n", pre, subs[i]);
        tprint(line);
        (*ndir)++;
        char sub[SC_MAX];
        if (dir[0]) kfmt(sub, sizeof sub, "%s/%s", dir, subs[i]);
        else        strlcpy(sub, subs[i], sizeof sub);
        flags[depth] = (unsigned char)(left == 0);
        tree_level(sub, flags, depth + 1, nfile, ndir);
    }
    for (int i = 0; i < nf; i++) {
        left--;
        FsEnt *e = fs_slot(fidx[i]);
        char pre[40], line[96];
        tr_prefix(flags, depth, left == 0, pre, sizeof pre);
        kfmt(line, sizeof line, "%s%s\n", pre, tr_leaf(e->name));
        tprint(line);
        (*nfile)++;
    }
}

static void do_tree(const char *root)
{
    if (!fs_ensure()) { tprint("disk error\n"); return; }
    char line[64];
    kfmt(line, sizeof line, "/%s\n", root);
    tprint(line);
    unsigned char flags[8];
    int nf = 0, nd = 0;
    tree_level(root, flags, 0, &nf, &nd);
    kfmt(line, sizeof line, "\n%d director%s, %d file%s\n",
         nd, nd == 1 ? "y" : "ies", nf, nf == 1 ? "" : "s");
    tprint(line);
}

static void do_ring3(void)
{
    char line[96];
    if (!api->ring3_ready || !api->ring3_ready()) {
        tprint("ring3: not initialised (needs paging + a v25 kernel)\n");
        return;
    }
    tprint("running payloads at CPL 3...\n\n");

    u32 r = api->ring3_test(0);
    if (r & 0x80000000u)
        kfmt(line, sizeof line, "syscall     FAULT vec %u err %u\n",
             (r >> 16) & 0x7FFF, r & 0xFFFF);
    else
        kfmt(line, sizeof line, "syscall     ok - int 80h returned %u, "
             "ring 3 exited cleanly\n", r);
    tprint(line);

    r = api->ring3_test(1);

    if (r & 0x40000000u)
        tprint("kernel write NOT BLOCKED - ring 3 wrote kernel memory\n");
    else {
        u32 vec = (r >> 16) & 0x7FFF, err = r & 0xFFFF;
        kfmt(line, sizeof line,
             "kernel write blocked - vec %u (#PF) err %u [%s%s%s]\n",
             vec, err,
             (err & 1) ? "protection" : "not-present",
             (err & 2) ? " write" : " read",
             (err & 4) ? " user" : " supervisor");
        tprint(line);
    }

    r = api->ring3_test(2);
    if (r & 0x80000000u) {
        kfmt(line, sizeof line,
             "port i/o    blocked - vec %u (%s), iopl 0 and no i/o bitmap\n",
             (r >> 16) & 0x7FFF,
             ((r >> 16) & 0x7FFF) == 13 ? "#GP" : "trap");
        tprint(line);
    } else tprint("port i/o    NOT BLOCKED - ring 3 reached the hardware\n");

    tprint("\nnothing else runs at ring 3 yet: the Kapi is direct calls,\n");
    tprint("so moving apps down needs a syscall interface first.\n");
}

static unsigned char dk_map[FH_HEADS][FH_CYLS];
static u32 dk_ok, dk_slow, dk_retry, dk_bad;
static u8  dk_scanned;

static void dk_reset(void)
{
    for (int h = 0; h < FH_HEADS; h++)
        for (int c = 0; c < FH_CYLS; c++) dk_map[h][c] = FH_UNTESTED;
    dk_ok = dk_slow = dk_retry = dk_bad = 0;
    dk_scanned = 0;
}

#define DK_BAND 40

static void dk_print_map(void)
{
    char row[DK_BAND + 2];
    tprint("\n  . untested   = ok   ~ slow   ? retried   X bad\n");
    for (int start = 0; start < FH_CYLS; start += DK_BAND) {
        dm_ruler(start, DK_BAND, row, sizeof row);
        tprint("\ncyl     ");
        tprint(row);
        tputc('\n');
        for (int h = 0; h < FH_HEADS; h++) {
            char lbl[12];
            kfmt(lbl, sizeof lbl, " head %d ", h);
            tprint(lbl);
            dm_row(dk_map[h], start, DK_BAND, row, sizeof row);
            tprint(row);
            tputc('\n');
        }
    }
}

static void dk_scan(void)
{
    tprint("verifying 2880 sectors (Esc aborts)...\n");
    dk_reset();
    u8 sec[512];
    int abort = 0;
    u32 lba = 0;
    api->esc_arm();
    for (; lba < FH_TOTAL && !abort; lba++) {
        u32 t0 = ticks;
        int rc = fdc_read(lba, sec);
        u32 ms = (u32)(ticks - t0) * 10;
        int cls = fh_class(rc, (int)api->disk_stat(DS_LAST_TRIES), ms);
        int c, h;
        if (fh_cell(lba, &c, &h))
            dk_map[h][c] = (unsigned char)fh_worst(dk_map[h][c], cls);
        if (cls == FH_BAD)        dk_bad++;
        else if (cls == FH_RETRY) dk_retry++;
        else if (cls == FH_SLOW)  dk_slow++;
        else                      dk_ok++;

        if ((lba % 18) == 17) gui_pump();
        if (api->esc_pending()) abort = 1;
        if ((lba % 288) == 287) {
            char p[40];
            kfmt(p, sizeof p, "  %u%%...\n", (lba + 1) * 100 / FH_TOTAL);
            tprint(p);
        }
    }
    dk_scanned = 1;
    char t[80];
    dm_tally(dk_ok, dk_slow, dk_retry, dk_bad, t, sizeof t);
    tprint(abort ? "aborted - the map shows only what was read\n" : "");
    tprint(t);
    tputc('\n');
    dk_print_map();
    int v = fh_verdict(dk_bad, dk_retry, dk_slow,
                       dk_ok + dk_slow + dk_retry + dk_bad);
    static const char *const verd[3] = {
        "\nverdict: this disk looks healthy\n",
        "\nverdict: aging - copy anything you care about\n",
        "\nverdict: FAILING - replace it\n" };
    tprint(verd[v]);
}

static void dk_seek(void)
{
    u8 sec[512];
    char line[80];
    tprint("cylinder:");
    for (int i = 0; i < 8; i++) {
        u32 cyl = (u32)i * (FH_CYLS - 1) / 7;
        kfmt(line, sizeof line, " %5u", cyl);
        tprint(line);
    }
    tprint("\n      ms:");
    for (int i = 0; i < 8; i++) {
        u32 cyl = (u32)i * (FH_CYLS - 1) / 7;
        fdc_read(0, sec);
        u32 t0 = ticks;
        fdc_read(cyl * FH_HEADS * FH_SECTORS, sec);
        kfmt(line, sizeof line, " %5u", (u32)(ticks - t0) * 10);
        tprint(line);
    }
    tputc('\n');
}

static void do_disk(const char *arg)
{
    while (*arg == ' ') arg++;
    if (!strcmp(arg, "scan")) { dk_scan(); return; }
    if (!strcmp(arg, "seek")) { dk_seek(); return; }
    if (arg[0] && strcmp(arg, "map")) {
        tprint("usage: disk [scan|seek|map]\n");
        return;
    }
    if (!strcmp(arg, "map")) {
        if (!dk_scanned)
            tprint("nothing scanned yet - run 'disk scan' first\n");
        dk_print_map();
        return;
    }

    char line[96];
    tprint("drive A: 1.44M, 80 cylinders x 2 heads x 18 sectors\n");
    kfmt(line, sizeof line, "operations   %u total, %u retried, %u failed\n",
         api->disk_stat(DS_OPS), api->disk_stat(DS_RETRIED),
         api->disk_stat(DS_FAILED));
    tprint(line);
    kfmt(line, sizeof line, "timing       slowest %u ms, last %u ms (%u tries)\n",
         api->disk_stat(DS_WORST_MS), api->disk_stat(DS_LAST_MS),
         api->disk_stat(DS_LAST_TRIES));
    tprint(line);
    kfmt(line, sizeof line, "last status  ST0 %02x  ST1 %02x  ST2 %02x  at LBA %u\n",
         api->disk_stat(DS_ST0), api->disk_stat(DS_ST1), api->disk_stat(DS_ST2),
         api->disk_stat(DS_LAST_LBA));
    tprint(line);

    tprint("spare        n/a - FLOPFS has no sector remapping\n");
    if (dk_scanned) {
        char t[80];
        dm_tally(dk_ok, dk_slow, dk_retry, dk_bad, t, sizeof t);
        kfmt(line, sizeof line, "last scan    %s\n", t);
        tprint(line);
        dk_print_map();
    } else tprint("\nrun 'disk scan' to read-verify the surface\n");
}


#include "shellfilters.inc"

static void sh_exec(char *cmd)
{
    while (*cmd == ' ') cmd++;

    {
        int n = (int)strlen(cmd);
        while (n > 0 && (cmd[n - 1] == ' ' || cmd[n - 1] == '\t')) cmd[--n] = 0;
    }
    if (!*cmd) return;
    char buf[96];

    if (api->ktrace) {
        char tl[80];
        kfmt(tl, sizeof tl, "shell: %s", cmd);
        api->ktrace(tl);
    }

    char c0[16];
    int ci = 0;
    while (cmd[ci] && cmd[ci] != ' ' && ci < 15) { c0[ci] = cmd[ci]; ci++; }
    c0[ci] = 0;
    const char *cargs = cmd + ci;
    while (*cargs == ' ') cargs++;
    if (!strcmp(cargs, "/help") || !strcmp(cargs, "-h") || !strcmp(cargs, "--help")) {
        const char *u = cmd_usage(c0);
        if (!u) u = sh_usage(c0);
        tprint(u ? u : "no arguments (or try 'help')");
        tputc('\n');
        return;
    }

    if(sf_exec(c0,cargs))return;

    if (!strcmp(cmd, "help")) {
        tprint("files: ls map cat rm cp mv touch hexdump wc head tail\n");
        tprint("       grep find sort uniq strings crc32 cmp stat edit open\n");
        tprint("filters: -h for options; > file writes, >> file appends\n");
        tprint("dirs:  cd pwd tree mkdir rmdir du\n");
        tprint("disk:  disk [scan|seek|map]  defrag fscan bootsec\n");
        tprint("usb:   uls ucat ucp urm\n");
        tprint("net:   ifconfig [ip]  ping <ip>  dns <host>  wget <url>\n");
        tprint("       netdiag  lspci\n");
        tprint("fun:   matrix rainbow beep\n");
        tprint("cfg:   set [video|mouse|net ...]  confsec\n");
        tprint("sys:   uname free df uptime date cal fetch clear ver\n");
        tprint("       whoami pwd dmesg usb ps kill calc history\n");
        tprint("       kext kupdate settings about reboot shutdown\n");
        tprint("       bios testram bench\n");
        tprint("any command accepts /help; PgUp/PgDn or wheel to scroll\n");
    } else if (!strcmp(cmd, "set") || !strncmp(cmd, "set ", 4)) {
        static const char *vnames[5] = { "?", "640x480", "800x600", "1024x768", "vga" };
        const char *a = cmd[3] ? cmd + 4 : "";
        while (*a == ' ') a++;
        if (!*a) {
            u8 vid = (CFG->video >= 1 && CFG->video <= 4) ? CFG->video : 1;
            static const int vw[5] = { 0, 640, 800, 1024, 320 };
            kfmt(buf, sizeof buf, "video: %s%s\n", vnames[vid],
                 vw[vid] == SW ? "" : " (takes effect after reboot)");
            tprint(buf);
            kfmt(buf, sizeof buf, "mouse: speed %d\n", CFG->mouse_speed);
            tprint(buf);
            kfmt(buf, sizeof buf, "net:   %s\n", CFG->net_mode ? "static" : "dhcp");
            tprint(buf);
            tprint("usage: set video 640|800|1024|vga\n");
            tprint("       set mouse 1-4\n");
            tprint("       set net dhcp | set net static <ip> [gw]\n");
        } else if (!strncmp(a, "video ", 6)) {
            const char *v = a + 6;
            u8 nv = 0;
            if (!strncmp(v, "640", 3)) nv = 1;
            else if (!strncmp(v, "800", 3)) nv = 2;
            else if (!strncmp(v, "1024", 4)) nv = 3;
            else if (!strcmp(v, "vga")) nv = 4;
            if (!nv) { tprint("usage: set video 640|800|1024|vga\n"); return; }
            CFG->video = nv;
            tprint(config_save() ? "saved - reboot to apply\n" : "disk error saving config\n");
        } else if (!strncmp(a, "mouse ", 6)) {
            int sp = a[6] - '0';
            if (sp < 1 || sp > 4) { tprint("usage: set mouse 1-4\n"); return; }
            CFG->mouse_speed = sp;
            tprint(config_save() ? "saved\n" : "disk error saving config\n");
        } else if (!strcmp(a, "net dhcp")) {
            CFG->net_mode = 0;
            if (!config_save()) { tprint("disk error saving config\n"); return; }
            if (net_up() && net_dhcp(1200)) {
                u32 lip = net_get(NET_IP);
                kfmt(buf, sizeof buf, "lease: %d.%d.%d.%d\n", lip & 0xFF,
                     (lip >> 8) & 0xFF, (lip >> 16) & 0xFF, lip >> 24);
                tprint(buf);
            } else tprint("saved (no DHCP answer right now)\n");
        } else if (!strncmp(a, "net static ", 11)) {
            char ipstr[16], gwstr[16];
            const char *p = a + 11;
            while (*p == ' ') p++;
            int i = 0; while (*p && *p != ' ' && i < 15) ipstr[i++] = *p++; ipstr[i] = 0;
            while (*p == ' ') p++;
            i = 0; while (*p && *p != ' ' && i < 15) gwstr[i++] = *p++; gwstr[i] = 0;
            u32 ip, gw = 0;
            if (!net_parse_ip(ipstr, &ip) || (gwstr[0] && !net_parse_ip(gwstr, &gw))) {
                tprint("usage: set net static <ip> [gateway]\n");
                return;
            }
            CFG->net_mode = 1;
            net_set(NET_IP, CFG->ip = ip);
            CFG->mask = net_get(NET_MASK) ? net_get(NET_MASK) : 0x00FFFFFF;
            if (gw) net_set(NET_GW, CFG->gw = gw);
            else CFG->gw = net_get(NET_GW);
            net_set(NET_DHCP_OK, 0);
            tprint(config_save() ? "saved and applied\n" : "applied (disk error saving)\n");
        } else {
            tprint("unknown setting (try 'set')\n");
        }
    } else if (!strcmp(cmd, "uname")) {
        tprint("Flopnix\n");
    } else if (!strcmp(cmd, "uname -a")) {
        os_release();
        tputc('\n');
    } else if (!strcmp(cmd, "clear") || !strcmp(cmd, "cls")) {
        api->term_clear();
    } else if (!strncmp(cmd, "echo ", 5)) {
        char *p = cmd + 5, *gt = 0;
        for (char *q = p; *q; q++)
            if (*q == '>') { gt = q; break; }
        if (gt) {
            int append = gt[1] == '>';
            char *end = gt;
            while (end > p && end[-1] == ' ') end--;
            char *raw = gt + 1 + append;
            while (*raw == ' ') raw++;
            if (!*raw || end == p) { tprint("usage: echo text >|>> file\n"); return; }

            char fn[FS_NAMELEN];
            resolve(raw, fn, sizeof fn);
            if (!fn[0]) { tprint("echo: bad path\n"); return; }
            int r;
            if (append) {
                int n = fs_read(fn, iobuf, IOBUF_SZ);

                if (n == FS_EIO) { tprint("echo: cannot read that file\n"); return; }
                if (n < 0) n = 0;
                int add = end - p;
                if (n + add + 1 > IOBUF_SZ) { tprint("file too big\n"); return; }
                if (n && iobuf[n - 1] != '\n') iobuf[n++] = '\n';
                memcpy(iobuf + n, p, add);
                r = fs_write(fn, iobuf, n + add);
            } else r = fs_write(fn, (u8 *)p, end - p);
            if (r == -2) tprint("disk full\n");
            else if (r == -3) tprint("echo: that name is a folder\n");
            else if (r) tprint("disk error\n");
        } else {
            tprint(p);
            tputc('\n');
        }
    } else if (!strcmp(cmd, "echo")) {
        tputc('\n');
    } else if (!strcmp(cmd, "free")) {
        u32 total = api->boot_info(BI_MEM_KB), used = used_kb();
        kfmt(buf, sizeof buf, "total %u KB, used %u KB, free %u KB\n",
             total, used, total - used);
        tprint(buf);
    } else if (!strcmp(cmd, "uptime")) {
        u32 s = ticks / 100;
        kfmt(buf, sizeof buf, "up %u:%02u:%02u, 1 user, load average: 0.00\n",
             s / 3600, (s / 60) % 60, s % 60);
        tprint(buf);
    } else if (!strncmp(cmd, "pack ", 5) || !strncmp(cmd, "unpack ", 7)) {

        int packing = cmd[0] == 'p';
        const char *a = cmd + (packing ? 5 : 7);
        while (*a == ' ') a++;
        if (!*a) { tprint("usage: pack <file> | unpack <file.pz>\n"); return; }
        char src[SC_MAX];
        resolve(a, src, sizeof src);
        u32 half = (u32)IOBUF_SZ / 2;
        int n = fs_read(src, iobuf, half);
        if (n < 0) { tprint("no such file\n"); return; }
        u8 *out = iobuf + half;
        gui_pump();
        char dst[SC_MAX];
        int rn;
        if (packing) {
            rn = (int)lz_pack(iobuf, (u32)n, out, half);
            if (rn <= 0) { tprint("pack: too big\n"); return; }
            kfmt(dst, sizeof dst, "%s.pz", src);
        } else {
            rn = lz_unpack(iobuf, (u32)n, out, half);
            if (rn < 0) { tprint("unpack: not a .pz file (or corrupt)\n"); return; }
            int L = (int)strlen(src);
            strlcpy(dst, src, sizeof dst);
            if (L > 3 && !strcmp(src + L - 3, ".pz")) dst[L - 3] = 0;
            else strlcpy(dst, "unpacked", sizeof dst);
        }
        gui_pump();
        if (fs_write(dst, out, (u32)rn) != 0) { tprint("write error (disk full?)\n"); return; }
        if (packing)
            kfmt(buf, sizeof buf, "%s -> %s  %d -> %d bytes (%d%%)\n",
                 src, dst, n, rn, n ? rn * 100 / n : 100);
        else
            kfmt(buf, sizeof buf, "%s -> %s  %d -> %d bytes\n", src, dst, n, rn);
        tprint(buf);
    } else if (!strcmp(cmd, "date")) {
        int h, m, s, D, M, Y;
        rtc_read(&h, &m, &s, &D, &M, &Y);
        kfmt(buf, sizeof buf, "%d-%02d-%02d %02d:%02d:%02d\n", Y, M, D, h, m, s);
        tprint(buf);
    } else if (!strcmp(cmd, "ntp") || !strncmp(cmd, "ntp ", 4)) {

        const char *a = cmd[3] ? cmd + 4 : "";
        while (*a == ' ') a++;
        if (*a == '+' || *a == '-') {
            int neg = (*a == '-'); a++;
            int hh = 0, mm = 0;
            while (*a >= '0' && *a <= '9') hh = hh * 10 + (*a++ - '0');
            if (*a == ':') { a++;
                while (*a >= '0' && *a <= '9') mm = mm * 10 + (*a++ - '0'); }
            int qh = hh * 4 + mm / 15;
            if (neg) qh = -qh;
            if (qh < -48 || qh > 56) { tprint("ntp: offset out of range\n"); return; }
            CFG->tz_qh = (i8)qh;
            if (!config_save()) tprint("(disk error saving the offset)\n");
        }
        if (!net_up()) { tprint("ntp: no network device\n"); return; }
        tprint("asking pool.ntp.org...\n");
        u32 ip = net_dns("pool.ntp.org", 300);
        if (!ip) { tprint("ntp: cannot resolve pool.ntp.org\n"); return; }
        u32 secs = api->net_sntp(ip, 400);
        if (!secs) { tprint("ntp: no answer\n"); return; }
        NtpTime t;
        if (!ntp_civil(secs, CFG->tz_qh, &t)) { tprint("ntp: bad timestamp\n"); return; }
        if (api->rtc_write(t.h, t.mi, t.s, t.d, t.mo, t.y) != 0) {
            tprint("ntp: RTC refused the value\n"); return;
        }
        int q = CFG->tz_qh, aq = q < 0 ? -q : q;
        kfmt(buf, sizeof buf, "clock set: %d-%02d-%02d %02d:%02d:%02d (UTC%c%d:%02d)\n",
             t.y, t.mo, t.d, t.h, t.mi, t.s,
             q < 0 ? '-' : '+', aq / 4, (aq % 4) * 15);
        tprint(buf);
    } else if (!strcmp(c0, "cd")) {
        char t[SC_MAX],spec[SC_MAX];int drive=0;
        if(!path_arg(cargs,&drive,t,sizeof t)){tprint("cd: invalid path\n");return;}
        if(drive){
            if(fat_list(t,fe_scratch,1)<0){tprint("cd: USB directory not found\n");return;}
            kfmt(spec,sizeof spec,"u:%s",t);if(strlen(t)+2>=sizeof spec||!api->shell_set_cwd(spec))tprint("cd: path is too long\n");return;
        }

        if (t[0] && !dir_exists(t)) {
            kfmt(buf, sizeof buf, "cd: no such directory: %s\n", t);
            tprint(buf);
        } else if (api->shell_set_cwd) {
            api->shell_set_cwd(t);
        }
    } else if (!strcmp(c0, "mkdir")) {
        char t[SC_MAX];int drive=0;
        if(!path_arg(cargs,&drive,t,sizeof t)){tprint("mkdir: invalid path\n");return;}
        if (!t[0]) tprint("usage: mkdir <name>\n");
        else if (drive?api->fat_mkdir(t):api->fs_mkdir(t)) tprint("mkdir: failed\n");
    } else if (!strcmp(c0, "rmdir")) {
        char t[SC_MAX];int drive=0;
        if(!path_arg(cargs,&drive,t,sizeof t)){tprint("rmdir: invalid path\n");return;}
        if(drive){if(api->fat_rmdir(t))tprint("rmdir: not empty, unavailable or read-only\n");return;}

        if (!t[0]) tprint("usage: rmdir <name>\n");
        else if (!api->fs_is_dir(t)) tprint("rmdir: not a directory\n");
        else if (api->fs_dir_count(t) > 0) tprint("rmdir: directory not empty\n");
        else fs_delete(t);
    } else if (!strcmp(c0, "tree")) {
        char root[SC_MAX];
        resolve(cargs, root, sizeof root);
        do_tree(root);
    } else if (!strcmp(c0, "du")) {
        char root[SC_MAX];
        resolve(cargs, root, sizeof root);
        if (!fs_ensure()) { tprint("disk error\n"); return; }
        u32 bytes = 0, sect = 0;
        int n = 0;
        for (int i = 0; i < FS_NFILES; i++) {
            FsEnt *e = fs_slot(i);
            if (!e->used || (e->attr & FS_ATTR_DIR)) continue;
            if (root[0] && !under(e->name, root)) continue;
            bytes += e->size;
            sect += e->nsect;
            n++;
        }
        char hs[16];
        human_size(bytes, hs, sizeof hs);
        kfmt(buf, sizeof buf, "%s in %d file%s (%u KB on disk) under /%s\n",
             hs, n, n == 1 ? "" : "s", sect / 2, root);
        tprint(buf);
    } else if (!strcmp(cmd, "ls") || !strcmp(c0, "ls")) {
        char path[96];int drive=0;
        if(!path_arg(cargs,&drive,path,sizeof path)){tprint("ls: invalid path\n");return;}
        if(drive){int n=fat_list(path,fe_scratch,64);if(n<0){tprint("ls: USB directory not found\n");return;}
            for(int i=0;i<n;i++){kfmt(buf,sizeof buf,"%6u %s%s\n",fe_scratch[i].size,fe_scratch[i].name,fe_scratch[i].is_dir?"/":"");tprint(buf);}return;}
        if (!fs_ensure()) { tprint("disk error\n"); return; }
        char dir[SC_MAX];
        resolve(cargs, dir, sizeof dir);

        char subs[16][SC_MAX];
        int fidx[FS_NFILES];
        int nd = child_dirs(dir, subs, 16);
        int n  = child_files(dir, fidx, FS_NFILES);
        for (int i = 0; i < nd; i++) {
            kfmt(buf, sizeof buf, "  <dir>  %s\n", subs[i]);
            tprint(buf);
        }
        for (int i = 0; i < n; i++) {
            FsEnt *e = fs_slot(fidx[i]);
            kfmt(buf, sizeof buf, "%6u  %s\n", e->size, tr_leaf(e->name));
            tprint(buf);
        }
        kfmt(buf, sizeof buf, "%d file%s, %d folder%s, %u KB free\n",
             n, n == 1 ? "" : "s", nd, nd == 1 ? "" : "s", fs_free_kb());
        tprint(buf);
    } else if (!strcmp(cmd, "map")) {
        if (!fs_ensure()) { tprint("disk error\n"); return; }
        int idx[FS_NFILES], m = 0;
        for (int i = 0; i < FS_NFILES; i++)
            if (fs_slot(i)->used) idx[m++] = i;
        for (int i = 1; i < m; i++) {
            int k = idx[i], j = i - 1;
            u16 ks = fs_slot(k)->start;
            while (j >= 0 && fs_slot(idx[j])->start > ks) { idx[j + 1] = idx[j]; j--; }
            idx[j + 1] = k;
        }
        tprint("first  last  sect  file\n");
        for (int i = 0; i < m; i++) {
            FsEnt *e = fs_slot(idx[i]);
            u32 end = e->start + (e->nsect ? e->nsect - 1 : 0);
            kfmt(buf, sizeof buf, "%5u  %4u  %4u  %s\n", e->start, end, e->nsect, e->name);
            tprint(buf);
        }
        kfmt(buf, sizeof buf, "%d file%s\n", m, m == 1 ? "" : "s");
        tprint(buf);
    } else if (!strncmp(cmd, "rm ", 3)) {
        char fn[96];int drive=0,dir=0;u32 size=0;
        if(!path_arg(cargs,&drive,fn,sizeof fn)||!path_info(drive,fn,&size,&dir)){tprint("rm: file not found\n");return;}
        if(dir){tprint("rm: use rmdir for empty folders\n");return;}
        if(drive?fat_delete(fn):fs_delete(fn))tprint("rm: unable to delete file\n");
    } else if (!strncmp(cmd, "cp ", 3) || !strncmp(cmd, "mv ", 3)) {
        copy_move(cargs,cmd[0]=='m');
    } else if (!strncmp(cmd, "touch ", 6)) {
        char path[96];int drive=0,dir=0;u32 size=0;
        if(!path_arg(cargs,&drive,path,sizeof path)){tprint("touch: invalid path\n");return;}
        if(path_info(drive,path,&size,&dir)){
            if(dir){tprint("touch: path is a folder\n");return;}
            if(!drive&&fs_touch(path))tprint("touch: could not update the timestamp\n");
            else if(drive)tprint("touch: USB file already exists; contents kept\n");
        }else if(drive?fat_write(path,(const u8 *)"",0):fs_write(path,(const u8 *)"",0))tprint("touch: could not create the file\n");
        api->broadcast("fs.changed","");
    } else if (!strncmp(cmd, "hexdump ", 8)) {
        char fn[96];int drive=0;if(!path_arg(cargs,&drive,fn,sizeof fn)){tprint("Invalid path\n");return;}
        int n = drive?fat_read(fn,iobuf,512):fs_read(fn, iobuf, 512);
        if (n < 0) { kfmt(buf, sizeof buf, "hexdump: %s: not found\n", fn); tprint(buf); return; }
        for (int off = 0; off < n; off += 16) {
            kfmt(buf, sizeof buf, "%04x  ", off);
            tprint(buf);
            for (int j = 0; j < 16; j++) {
                if (off + j < n) { kfmt(buf, sizeof buf, "%02x ", iobuf[off + j]); tprint(buf); }
                else tprint("   ");
            }
            tprint(" ");
            for (int j = 0; j < 16 && off + j < n; j++) {
                char cc = iobuf[off + j];
                char s[2] = { (cc >= 32 && cc < 127) ? cc : '.', 0 };
                tprint(s);
            }
            tputc('\n');
        }
        if (n == 512) tprint("...(first 512 bytes)\n");
    } else if (!strncmp(cmd, "edit ", 5)) {
        open_path(cargs,1);
    } else if (!strcmp(c0,"open")) {
        if(!*cargs)tprint("usage: open <path>\n");else open_path(cargs,0);
    } else if (!strcmp(cmd, "matrix")) {
        api->term_fx(1);
    } else if (!strcmp(cmd, "rainbow")) {
        tprint(api->term_fx(2) ? "rainbow on\n" : "rainbow off\n");
    } else if (!strcmp(cmd, "ver")) {
        os_release(); tputc('\n');
    } else if (!strcmp(cmd, "whoami")) {
        tprint("root\n");
    } else if (!strcmp(cmd, "pwd")) {
        const char *cwd=api->shell_cwd();
        kfmt(buf,sizeof buf,cwd[0]&&cwd[1]==':'?"%s\n":"A:/%s\n",cwd);
        tprint(buf);
    } else if (!strcmp(cmd, "beep") || !strncmp(cmd, "beep ", 5)) {
        if (!timer_alive) { tprint("beep: no timer (E10) - skipped\n"); return; }
        int ms = cargs[0] ? 0 : 150;
        for (const char *p = cargs; *p >= '0' && *p <= '9'; p++) ms = ms * 10 + (*p - '0');
        if (ms < 20) ms = 150; if (ms > 2000) ms = 2000;
        outb(0x43, 0xB6);
        u32 div = 1193182 / 880;
        outb(0x42, div & 0xFF); outb(0x42, div >> 8);
        outb(0x61, inb(0x61) | 3);
        u32 t0 = ticks, guard = 0;
        while ((u32)(ticks - t0) < (u32)(ms / 10) + 1 && ++guard < 200000000u)
            if (gui_pump()) break;
        outb(0x61, inb(0x61) & ~3);
    } else if (!strncmp(cmd, "find ", 5)) {
        const char *pat = cmd + 5; while (*pat == ' ') pat++;
        if (!fs_ensure()) { tprint("disk error\n"); return; }
        int hits = 0;
        for (int i = 0; i < FS_NFILES; i++) {
            FsEnt *e = fs_slot(i);
            if (!e->used) continue;
            for (const char *s = e->name; *s; s++) {
                int k = 0; while (pat[k] && s[k] == pat[k]) k++;
                if (!pat[k]) { kfmt(buf, sizeof buf, "%s\n", e->name); tprint(buf); hits++; break; }
            }
        }
        if (!hits) tprint("(no matches)\n");
    } else if (!strcmp(cmd, "uls") || !strncmp(cmd, "uls ", 4)) {
        if (!fat_mount()) { tprint("no FAT volume on USB\n"); return; }
        const char *path = cargs[0] ? cargs : "/";
        int n = fat_list(path, fe_scratch, 64);
        if (n < 0) { tprint("path not found\n"); return; }
        for (int i = 0; i < n; i++) {
            char hs[16];
            if (fe_scratch[i].is_dir) strlcpy(hs, "<dir>", sizeof hs);
            else human_size(fe_scratch[i].size, hs, sizeof hs);
            kfmt(buf, sizeof buf, "%8s  %s\n", hs, fe_scratch[i].name);
            tprint(buf);
        }
        kfmt(buf, sizeof buf, "%d items on USB %s\n", n, fat_label());
        tprint(buf);
    } else if (!strncmp(cmd, "ucat ", 5)) {
        const char *p = cmd + 5; while (*p == ' ') p++;
        char path[80];
        if (p[0] != '/') kfmt(path, sizeof path, "/%s", p); else strlcpy(path, p, sizeof path);
        int n = fat_read(path, iobuf, IOBUF_SZ - 1);
        if (n < 0) { kfmt(buf, sizeof buf, "ucat: %s: not found\n", p); tprint(buf); return; }
        for (int i = 0; i < n; i++) { if (iobuf[i] != '\r') tputc(iobuf[i]); }
        if (n && iobuf[n - 1] != '\n') tputc('\n');
        if (n == IOBUF_SZ - 1) tprint("...(truncated)\n");
    } else if (!strncmp(cmd, "ucp ", 4)) {
        char a[80], b[FS_NAMELEN];
        const char *p = cmd + 4; while (*p == ' ') p++;
        int i = 0; while (*p && *p != ' ' && i < 78) a[i++] = *p++; a[i] = 0;
        while (*p == ' ') p++;
        i = 0; while (*p && *p != ' ' && i < FS_NAMELEN - 1) b[i++] = *p++; b[i] = 0;
        if (!a[0] || !b[0]) { tprint("usage: ucp <usbfile> <A:name>\n"); return; }
        char path[84];
        if (a[0] != '/') kfmt(path, sizeof path, "/%s", a); else strlcpy(path, a, sizeof path);
        int n = fat_read(path, iobuf, IOBUF_SZ);
        if (n < 0) { tprint("ucp: usb file not found\n"); return; }
        int wr = fs_write(b, iobuf, n);
        tprint(wr == 0 ? "copied to A:\n" : (wr == -2 ? "A: full\n" : "A: write error\n"));
    } else if (!strncmp(cmd, "urm ", 4)) {
        const char *p = cmd + 4; while (*p == ' ') p++;
        char path[80];
        if (p[0] != '/') kfmt(path, sizeof path, "/%s", p); else strlcpy(path, p, sizeof path);
        tprint(fat_delete(path) == 0 ? "deleted from USB\n" : "urm: failed (not found / read-only)\n");
    } else if (!strcmp(cmd, "usb")) {
        if (!usb_present()) { tprint("no USB storage device found\n"); return; }
        kfmt(buf, sizeof buf, "usb0: %s\n", usb_model());
        tprint(buf);
        char cap[16]; human_size_kb(usb_capacity_kb(), cap, sizeof cap);
        kfmt(buf, sizeof buf, "      %s", cap);
        tprint(buf);
        if (fat_mount()) { kfmt(buf, sizeof buf, ", FAT volume '%s'\n", fat_label()); tprint(buf); }
        else tprint(", no FAT filesystem\n");

    } else if (!strcmp(cmd, "ifconfig") || !strncmp(cmd, "ifconfig ", 9)) {
        if (!net_up()) { tprint("eth0: no network device found\n"); return; }
        const char *arg = cmd[8] ? cmd + 9 : 0;
        if (arg) {
            while (*arg == ' ') arg++;
            u32 ip;
            if (!net_parse_ip(arg, &ip)) { tprint("usage: ifconfig a.b.c.d\n"); return; }
            net_set(NET_IP, ip);
            net_set(NET_DHCP_OK, 0);
        }
        kfmt(buf, sizeof buf, "eth0: hwaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
             net_mac_get()[0], net_mac_get()[1], net_mac_get()[2],
             net_mac_get()[3], net_mac_get()[4], net_mac_get()[5]);
        tprint(buf);
        kfmt(buf, sizeof buf, "      inet %d.%d.%d.%d  mask %d.%d.%d.%d  (%s)\n",
             net_get(NET_IP) & 0xFF, (net_get(NET_IP) >> 8) & 0xFF,
             (net_get(NET_IP) >> 16) & 0xFF, net_get(NET_IP) >> 24,
             net_get(NET_MASK) & 0xFF, (net_get(NET_MASK) >> 8) & 0xFF,
             (net_get(NET_MASK) >> 16) & 0xFF, net_get(NET_MASK) >> 24,
             net_get(NET_DHCP_OK) ? "DHCP" : "static");
        tprint(buf);
        kfmt(buf, sizeof buf, "      gateway %d.%d.%d.%d\n",
             net_get(NET_GW) & 0xFF, (net_get(NET_GW) >> 8) & 0xFF,
             (net_get(NET_GW) >> 16) & 0xFF, net_get(NET_GW) >> 24);
        tprint(buf);

        u32 lk = net_get(NET_LINK);
        if (lk & NET_LINK_UP) {
            kfmt(buf, sizeof buf, "      link up, %u Mbit/s %s duplex\n",
                 (unsigned)NET_LINK_MBPS(lk),
                 (lk & NET_LINK_FULL) ? "full" : "half");
            tprint(buf);
        } else if (lk & NET_LINK_PHY) {
            tprint("      link DOWN - check the cable and the switch port\n");
        }
        u32 dns = net_get(NET_DNS);
        if (dns) {
            kfmt(buf, sizeof buf, "      dns %d.%d.%d.%d\n",
                 dns & 0xFF, (dns >> 8) & 0xFF, (dns >> 16) & 0xFF, dns >> 24);
            tprint(buf);
        }
    } else if (!strncmp(cmd, "dns ", 4)) {
        const char *name = cmd + 4;
        while (*name == ' ') name++;
        if (!*name) { tprint("usage: dns <hostname>\n"); return; }
        if (!net_up()) { tprint("dns: no network device\n"); return; }
        u32 ip = net_dns(name, 300);
        if (!ip) { kfmt(buf, sizeof buf, "dns: could not resolve %s\n", name); tprint(buf); }
        else kfmt(buf, sizeof buf, "%s has address %d.%d.%d.%d\n", name,
                  ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
        if (ip) tprint(buf);
    } else if (!strncmp(cmd, "wget ", 5)) {
        do_wget(cmd + 5);
    } else if (!strncmp(cmd, "ping ", 5)) {
        u32 dst;
        const char *host = cmd + 5;
        while (*host == ' ') host++;
        if (!*host) { tprint("usage: ping <a.b.c.d | hostname>\n"); return; }
        if (!net_up()) { tprint("ping: no network device\n"); return; }
        if (!net_parse_ip(host, &dst)) {
            dst = net_dns(host, 300);
            if (!dst) { kfmt(buf, sizeof buf, "ping: cannot resolve %s\n", host); tprint(buf); return; }
        }
        kfmt(buf, sizeof buf, "PING %s (%d.%d.%d.%d) 40 bytes\n", host,
             dst & 0xFF, (dst >> 8) & 0xFF, (dst >> 16) & 0xFF, dst >> 24);
        tprint(buf);
        int ms = net_ping(dst, 200);
        if (ms == -2)      tprint("host unreachable (no ARP reply)\n");
        else if (ms == -1) tprint("request timed out\n");
        else if (ms < 10)  { kfmt(buf, sizeof buf, "reply: time<10ms\n"); tprint(buf); }
        else               { kfmt(buf, sizeof buf, "reply: time=%dms\n", ms); tprint(buf); }
    } else if (!strcmp(cmd, "fetch")) {
        do_fetch();
    } else if (!strcmp(cmd, "dmesg")) {
        api->dmesg();
    } else if (!strcmp(cmd, "threads")) {
        api->threads();
    } else if (!strcmp(cmd, "kupdate") || !strncmp(cmd, "kupdate ", 8)) {
        const char *f = cmd[7] ? cmd + 8 : "";
        while (*f == ' ') f++;
        if (!*f) { tprint("usage: kupdate <file.ku | http://host/file.ku>\n"); return; }
        char kerr[48];
        if (!strncmp(f, "http://", 7)) {

            char host[64], path[96]; u16 port;
            if (!nw_url_parse(f, host, sizeof host, &port, path, sizeof path)) {
                tprint("kupdate: bad URL\n"); return;
            }
            if (!net_up()) { tprint("kupdate: no network device\n"); return; }
            u32 ip = net_dns(host, 300);
            if (!ip) { kfmt(buf, sizeof buf, "kupdate: cannot resolve %s\n", host);
                       tprint(buf); return; }
            kupd_n = 0; kupd_over = 0;
            tprint("downloading...\n");
            int st = net_http_get(ip, port, host, path, kupd_sink, 0, 1500);
            if (st == -2) { tprint("kupdate: no network device\n"); return; }
            if (st == -3) { tprint("kupdate: cannot reach that address\n"); return; }
            if (st == -4) {
                kfmt(buf, sizeof buf,
                     "kupdate: download cut short at %u bytes - server or link\n",
                     kupd_n);
                tprint(buf);
                return;
            }
            if (st < 0)   { tprint("kupdate: no response\n"); return; }
            if (kupd_over){ tprint("kupdate: file too large\n"); return; }
            if (st != 200){ kfmt(buf, sizeof buf, "kupdate: http %d\n", st);
                            tprint(buf); return; }
            kfmt(buf, sizeof buf, "got %u bytes, verifying...\n", kupd_n);
            tprint(buf);
            if (api->kernel_update_data(iobuf, kupd_n, kerr, sizeof kerr) != 0) {
                kfmt(buf, sizeof buf, "kupdate: %s\n", kerr);
                tprint(buf);
            }
            return;
        }
        if (kernel_update(f, kerr, sizeof kerr) != 0) {
            kfmt(buf, sizeof buf, "kupdate: %s\n", kerr);
            tprint(buf);
        }
    } else if (!strcmp(cmd, "df")) {
        tprint("drive       size      used      free\n");
        u32 tot = (2880 - 288) / 2;
        u32 fr = fs_ensure() ? fs_free_kb() : 0;
        char t2[16], u2[16], f2[16];
        kfmt(t2, sizeof t2, "%u KB", tot);
        kfmt(u2, sizeof u2, "%u KB", tot - fr);
        kfmt(f2, sizeof f2, "%u KB", fr);
        kfmt(buf, sizeof buf, "A:     %9s %9s %9s\n", t2, u2, f2);
        tprint(buf);
        if (fat_mount()) {
            u32 vt = fat_total_kb(), vf = fat_free_kb();
            human_size_kb(vt, t2, sizeof t2);
            human_size_kb(vt - vf, u2, sizeof u2);
            human_size_kb(vf, f2, sizeof f2);
            kfmt(buf, sizeof buf, "usb0:  %9s %9s %9s\n", t2, u2, f2);
            tprint(buf);
        }
    } else if (!strcmp(cmd, "cal")) {
        int h, m, s, D, M, Y;
        rtc_read(&h, &m, &s, &D, &M, &Y);
        static const char *mn[12] = {
            "January", "February", "March", "April", "May", "June", "July",
            "August", "September", "October", "November", "December"
        };
        static const u8 mdl[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        int days = mdl[M - 1];
        if (M == 2 && Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0)) days = 29;
        static const u8 sak[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
        int y = Y - (M < 3);
        int dow = (y + y / 4 - y / 100 + y / 400 + sak[M - 1] + 1) % 7;
        kfmt(buf, sizeof buf, "      %s %d\n", mn[M - 1], Y);
        tprint(buf);
        tprint("Su Mo Tu We Th Fr Sa\n");
        int d = 1;
        while (d <= days) {
            char row[26]; int ri = 0;
            for (int c2 = 0; c2 < 7 && d <= days; c2++) {
                if (d == 1 && c2 < dow) {
                    row[ri++] = ' '; row[ri++] = ' '; row[ri++] = ' ';
                    continue;
                }
                char cell[6];
                kfmt(cell, sizeof cell, "%2d%c", d, d == D ? '*' : ' ');
                row[ri++] = cell[0]; row[ri++] = cell[1]; row[ri++] = cell[2];
                d++;
            }
            row[ri] = 0;
            tprint(row);
            tputc('\n');
        }
    } else if (!strncmp(cmd, "stat ", 5)) {
        char path[96];int drive=0,dir=0;u32 size=0;
        if(!path_arg(cargs,&drive,path,sizeof path)||!path_info(drive,path,&size,&dir)){tprint("stat: path not found\n");return;}
        kfmt(buf,sizeof buf,"%s:%s%s\n",drive?"U":"A",drive?"":"/",path);tprint(buf);
        kfmt(buf,sizeof buf,"%s, %u bytes\n",dir?"Directory":"File",size);tprint(buf);
        if(!drive)for(int i=0;i<FS_NFILES;i++){
            FsEnt *e=fs_slot(i);if(!e||!e->used||strcmp(e->name,path))continue;char dt[16];dos_fmt(e->mtime,dt);
            kfmt(buf,sizeof buf,"Modified %s, LBA %u, %u sectors\n",dt,e->start,e->nsect);tprint(buf);break;
        }
    } else if (!strcmp(cmd, "history")) {
        const char *h;
        int i = 0;
        for (; (h = api->term_hist(i)) != 0; i++) {
            kfmt(buf, sizeof buf, "%3d  %s\n", i + 1, h);
            tprint(buf);
        }
        if (!i) tprint("(no history yet)\n");
    } else if (!strncmp(cmd, "calc ", 5)) {
        const char *e = cmd + 5;
        calc_err = 0;
        i32 v = calc_expr(&e);
        while (*e == ' ') e++;
        if (!calc_err && *e) calc_err = "bad expression";
        if (calc_err) { kfmt(buf, sizeof buf, "calc: %s\n", calc_err); tprint(buf); return; }
        if (v < 0) kfmt(buf, sizeof buf, "= %d\n", v);
        else       kfmt(buf, sizeof buf, "= %d (0x%x)\n", v, (u32)v);
        tprint(buf);
    } else if (!strcmp(cmd, "ps")) {
        tprint(" id  window\n");
        int n = win_max();
        for (int i = 0; i < n; i++) {
            const Win *w = win_slot(i);
            if (!w || !w->used) continue;
            const char *ttl = w->tbuf_on ? w->tbuf : w->title;
            kfmt(buf, sizeof buf, "%3d  %s (inst %d)\n", i, ttl, w->inst);
            tprint(buf);
        }
    } else if (!strncmp(cmd, "kill ", 5)) {
        const char *p = cmd + 5; while (*p == ' ') p++;
        int id = 0, any = 0;
        while (*p >= '0' && *p <= '9') { id = id * 10 + (*p++ - '0'); any = 1; }
        if (!any) { tprint("usage: kill <id>  (see 'ps')\n"); return; }
        int r = api->win_close(id);
        if (r == 1)      { kfmt(buf, sizeof buf, "closed window %d\n", id); tprint(buf); }
        else if (r == -1) tprint("kill: that's this terminal\n");
        else              tprint("kill: bad window id (see 'ps')\n");
    } else if (!strcmp(cmd, "lspci")) {
        int found = 0;
        for (int bus = 0; bus < 8; bus++)
        for (int dev = 0; dev < 32; dev++)
        for (int fn = 0; fn < 8; fn++) {
            u32 addr = 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8);
            outl(0xCF8, addr);
            u32 id = inl(0xCFC);
            if ((id & 0xFFFF) == 0xFFFF) { if (fn == 0) break; else continue; }
            outl(0xCF8, addr | 8);
            u32 cls = inl(0xCFC) >> 24;
            const char *cn = "device";
            switch (cls) {
            case 0x01: cn = "storage"; break;
            case 0x02: cn = "network"; break;
            case 0x03: cn = "display"; break;
            case 0x04: cn = "multimedia"; break;
            case 0x05: cn = "memory"; break;
            case 0x06: cn = "bridge"; break;
            case 0x0C: cn = "serial bus / USB"; break;
            }
            kfmt(buf, sizeof buf, "%02x:%02x.%d  %04x:%04x  %s\n",
                 bus, dev, fn, id & 0xFFFF, id >> 16, cn);
            tprint(buf);
            found++;
            if (fn == 0) {
                outl(0xCF8, addr | 0x0C);
                if (!((inl(0xCFC) >> 16) & 0x80)) break;
            }
        }
        if (!found) tprint("no PCI devices found\n");
    } else if (!strcmp(cmd, "settings")) {
        int st = app_find("Settings");
        if (st >= 0) win_open(st);
        else tprint("settings: not loaded (settings.kx missing?)\n");
    } else if (!strcmp(cmd, "about")) {
        int t = app_find("About");
        if (t >= 0) win_open(t);
        else tprint("about: not loaded (about.kx missing?)\n");
    } else if (!strcmp(cmd, "kext") || !strncmp(cmd, "kext ", 5)) {
        if (!strncmp(cargs, "load ", 5)) {
            const char *fn = cargs + 5;
            while (*fn == ' ') fn++;
            int r = kext_load(fn);
            if (r == 0) tprint("loaded\n");
            else { kfmt(buf, sizeof buf, "load failed (E%d)\n", r); tprint(buf); }
            return;
        }
        if (!kext_count()) { tprint("no extensions loaded\n"); return; }
        tprint("extension     kind    size   status\n");
        for (int i = 0; i < kext_count(); i++) {
            const KextInfo *k = kext_get(i);
            char st[12];
            if(k->status==47)strlcpy(st,"on demand",sizeof st);
            else if(k->status==46)strlcpy(st,"inactive",sizeof st);
            else if (k->status) kfmt(st, sizeof st, "E%d", k->status);
            else strlcpy(st, "ok", sizeof st);
            kfmt(buf, sizeof buf, "%s", k->name);
            tprint(buf);
            for (u32 sp = strlen(k->name); sp < 14; sp++) tputc(' ');
            const char *kind = k->kind == KEXT_KIND_KERNEL ? "kernel" :
                               k->kind == KEXT_KIND_APP ? "app" : "-";
            kfmt(buf, sizeof buf, "%s", kind);
            tprint(buf);
            for (u32 sp = strlen(kind); sp < 8; sp++) tputc(' ');
            kfmt(buf, sizeof buf, "%u B   %s\n", k->size, st);
            tprint(buf);
        }
    } else if (!strcmp(cmd, "defrag")) {
        tprint("defragmenting A: (do not remove the disk)...\n");
        int n = fs_defrag(defrag_prog);

        if (n == -1)      tprint("defrag: stopped - a sector would not read;\n"
                                 "all files intact (dmesg names the file)\n");
        else if (n == -2) tprint("defrag: stopped - a write failed; the file\n"
                                 "was put back intact, but this disk is suspect\n");
        else if (n < 0)   tprint("defrag: stopped - a write failed and the\n"
                                 "copy-back failed too: ONE FILE IS DAMAGED\n"
                                 "(dmesg names it - copy it off and check it)\n");
        else if (n == 0) tprint("already contiguous - nothing to do\n");
        else { kfmt(buf, sizeof buf, "done: %d file%s moved\n", n, n == 1 ? "" : "s"); tprint(buf); }
    } else if (!strcmp(cmd, "ring3")) {
        do_ring3();
    } else if (!strcmp(cmd, "disk") || !strncmp(cmd, "disk ", 5)) {
        do_disk(cmd[4] ? cmd + 5 : "");
    } else if (!strcmp(cmd, "fscan")) {
        tprint("scanning 2880 sectors (Esc aborts)...\n");
        u8 sec[512];
        int bad = 0, shown = 0, abort = 0;
        api->esc_arm();
        for (int lba = 0; lba < 2880 && !abort; lba++) {
            if (fdc_read((u32)lba, sec) != 0) {
                bad++;
                if (shown < 8) { kfmt(buf, sizeof buf, "  bad sector at LBA %d\n", lba); tprint(buf); shown++; }
            }
            if ((lba & 0xFF) == 0xFF) {
                kfmt(buf, sizeof buf, "  %d%%...\n", lba * 100 / 2880);
                tprint(buf);
                gui_pump();
            }

            if (api->esc_pending()) abort = 1;
        }
        if (abort) tprint("aborted\n");
        else if (!bad) tprint("all 2880 sectors readable\n");
        else { kfmt(buf, sizeof buf, "%d bad sector%s found\n", bad, bad == 1 ? "" : "s"); tprint(buf); }
    } else if (!strcmp(cmd, "bootsec")) {
        u8 sec[512];
        if (fdc_read(0, sec) != 0) { tprint("bootsec: read error\n"); return; }
        static const char hx[] = "0123456789abcdef";
        for (int off = 0; off < 512; off += 16) {
            char *p = buf;
            *p++ = hx[(off >> 8) & 15]; *p++ = hx[(off >> 4) & 15];
            *p++ = hx[off & 15]; *p++ = ' '; *p++ = ' ';
            for (int i = 0; i < 16; i++) {
                *p++ = hx[sec[off + i] >> 4];
                *p++ = hx[sec[off + i] & 15];
                *p++ = ' ';
            }
            *p++ = ' ';
            for (int i = 0; i < 16; i++)
                *p++ = (sec[off + i] >= 32 && sec[off + i] < 127) ? (char)sec[off + i] : '.';
            *p++ = '\n'; *p = 0;
            tprint(buf);
        }
        kfmt(buf, sizeof buf, "signature: %02x %02x (%s)\n", sec[510], sec[511],
             sec[510] == 0x55 && sec[511] == 0xAA ? "bootable" : "NOT bootable");
        tprint(buf);
    } else if (!strcmp(cmd, "bios")) {
        const u8 *rom = (const u8 *)0xF0000;
        char date[9];
        for (int i = 0; i < 8; i++) {
            char c = (char)rom[0xFFF5 + i];
            date[i] = (c >= 32 && c < 127) ? c : '.';
        }
        date[8] = 0;
        kfmt(buf, sizeof buf, "date:    %s   model %02xh\n", date, rom[0xFFFE]);
        tprint(buf);

        kfmt(buf, sizeof buf, "ebda:    %04x0h   base mem: %u KB\n",
             api->boot_info(BI_EBDA_SEG), api->boot_info(BI_BASE_MEM_KB));
        tprint(buf);
        kfmt(buf, sizeof buf, "memory:  %u MB (E801/E820)\n", api->boot_info(BI_MEM_KB) / 1024);
        tprint(buf);
    } else if (!strcmp(cmd, "shutdown")) {
        tprint("shutting down...\n");
        config_save();
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        outw(0x4004, 0x3400);

        fill_rect(0, 0, SW, SH, C_BLACK);
        const char *m = "It is now safe to turn off your computer.";
        draw_text(SW / 2 - (int)strlen(m) * 4, SH / 2, m, C_YELLOW);
        flip();
        for (;;) { __asm__ volatile("cli; hlt"); }
    } else if (!strcmp(cmd, "testram")) {
        u32 top = api->boot_info(BI_MEM_KB) * 1024u;
        u32 lo = 0x00800000u;
        if (top <= lo + 0x10000) { tprint("testram: needs more than 8 MB of RAM\n"); return; }
        kfmt(buf, sizeof buf, "testing %u KB (8 MB..top), 3 passes, Esc aborts\n",
             (top - lo) / 1024);
        tprint(buf);
        int abort = 0, fail = 0;
        u32 skipped = 0;
        api->esc_arm();
        for (int pass = 0; pass < RT_PASSES && !abort && !fail; pass++) {
            kfmt(buf, sizeof buf, "pass %d: writing...\n", pass + 1);
            tprint(buf);
            skipped = 0;
            for (u32 a = lo; a < top && !abort; a += 0x10000) {
                u32 wds = (top - a < 0x10000 ? top - a : 0x10000) / 4;
                if (rt_usable(a)) rt_fill((u32 *)a, wds, a, pass);
                else skipped++;
                gui_pump();
                if (api->esc_pending()) abort = 1;
            }
            kfmt(buf, sizeof buf, "pass %d: verifying...\n", pass + 1);
            tprint(buf);
            for (u32 a = lo; a < top && !abort && !fail; a += 0x10000) {
                u32 wds = (top - a < 0x10000 ? top - a : 0x10000) / 4;
                if (!rt_usable(a)) { gui_pump(); if (api->esc_pending()) abort = 1; continue; }
                i32 bad = rt_check((const u32 *)a, wds, a, pass);
                if (bad >= 0) {
                    kfmt(buf, sizeof buf, "FAULT at %x (pass %d)\n",
                         a + (u32)bad * 4, pass + 1);
                    tprint(buf);
                    fail = 1;
                }
                gui_pump();
                if (api->esc_pending()) abort = 1;
            }
        }
        if (skipped) {
            kfmt(buf, sizeof buf, "skipped %u KB the kernel keeps unmapped\n",
                 skipped * 64u);
            tprint(buf);
        }
        if (abort)     tprint("aborted\n");
        else if (fail) tprint("MEMORY IS FAULTY - do not trust this machine\n");
        else           tprint("memory ok\n");
    } else if (!strcmp(cmd, "reboot")) {
        reboot();
    } else if (!strcmp(cmd, "crash")) {
        tprint("triggering a test exception (P6)...\n");
        __asm__ volatile("ud2");
    } else if (sh_usage(c0)) {

        kfmt(buf, sizeof buf, "usage: %s\n", sh_usage(c0));
        tprint(buf);
    } else {
        int file_command=0;for(const char *p=cmd;*p;p++)if(*p=='.'||*p=='/'||*p=='\\'||*p==':'){file_command=1;break;}
        if(file_command){open_path(cmd,0);return;}
        kfmt(buf, sizeof buf, "%s: command not found (try 'help')\n", cmd);
        tprint(buf);
    }
}

static void shell_dispatch(const char *line)
{
    char buf[192];
    strlcpy(buf, line, sizeof buf);
    sh_exec(buf);
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "Shell"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    api->register_shell(shell_dispatch);
    return 0;
}
