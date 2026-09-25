/* Draws desktop files and handles their menus and drag actions. */
#include "kapi.h"
#include "fspath.inc"
#include "archiveicon.inc"
#include "foldercopy.inc"
#include "shpath.h"
#include "fileops.inc"
#include "fileopen.inc"
#include "deskpath.inc"
#include "desklst.inc"
#include "textfield.inc"
#include "gdi.h"
#include "bmp.inc"
#include "wallpaper.inc"
#include "marquee.inc"

static const Kapi *api;
#include "fileprops.inc"

#define DESK_LST "desktop.lst"
#define DMAX FS_NFILES
static char names[DMAX][FS_NAMELEN];
static int nnames, vis[DMAX], nvis;
static u8 loaded, sync_forced;
static u32 sync_t;

static void lst_touch(void) { sync_forced = 1;api->broadcast("file.changed",""); }
static void lst_sync(void) {
    if (!loaded || (!sync_forced && (u32)(*api->ticks - sync_t) < 25)) return;
    sync_forced = 0; sync_t = *api->ticks; nnames = nvis = 0;
    for (int i = 0; i < FS_NFILES && nnames < DMAX; i++) {
        FsEnt *e = api->fs_slot(i);
        if (!e || !e->used) continue;
        char child[FS_NAMELEN];if(!fs_child(e->name,"desktop",child,sizeof child))continue;
        int have=0;for(int j=0;j<nnames;j++)if(!api->strcmp(names[j],child)){have=1;break;}
        if(have)continue;
        api->strlcpy(names[nnames], child, FS_NAMELEN);
        vis[nvis++] = nnames++;
    }
}
static void lst_add(const char *nm) { (void)nm; lst_touch(); }
static void lst_del(const char *nm) { (void)nm; lst_touch(); }

static void lst_load(void) {
    loaded = 1;
    if (!api->fs_is_dir("desktop") && api->fs_mkdir("desktop") != 0)
        api->klog("desktop: could not create the desktop folder\n");
    if (api->fs_exists(DESK_LST) && !api->fs_exists("desktop.old")) {
        char buf[2048];
        int n = api->fs_read(DESK_LST, (u8 *)buf, sizeof buf - 1), ok = n >= 0, serial = 0;
        if (n >= 0) {
            buf[n] = 0;
            char *p = buf;
            while (*p) {
                char *end = p; while (*end && *end != '\n') end++;
                char more = *end; *end = 0;
                if (end > p && end[-1] == '\r') end[-1] = 0;
                u32 old_start; const char *name;
                if (dl_parse(p, &old_start, &name)) {
                    char source[FS_NAMELEN]; api->strlcpy(source, name, sizeof source);
                    if (!api->fs_exists(source) && old_start)
                        for (int i = 0; i < FS_NFILES; i++) {
                            FsEnt *e = api->fs_slot(i);
                            if (e->used && e->start == old_start) { api->strlcpy(source, e->name, sizeof source); break; }
                        }
                    if (api->fs_exists(source) && !api->fs_is_dir(source) && !desk_member(source)) {
                        char dst[FS_NAMELEN] = {0}, ext[7], shortname[16];
                        api->strlcpy(ext, api->path_ext(source), sizeof ext);
                        int bytes = api->fs_read(source, api->iobuf, api->iobuf_size);
                        if (bytes < 0) ok = 0;
                        else {
                            u32 crc = api->crc32(api->iobuf, bytes);
                            desk_path(dst, desk_leaf(source), sizeof dst);
                            int copied = 0;
                            for (int attempt = 0; attempt < FS_NFILES; attempt++) {
                                if (dst[0] && api->fs_exists(dst)) {
                                    int old = api->fs_read(dst, api->iobuf, api->iobuf_size);
                                    if (old == bytes && api->crc32(api->iobuf, old) == crc) { copied = 1; break; }
                                } else if (dst[0]) {
                                    int nr = api->fs_read(source, api->iobuf, api->iobuf_size);
                                    copied = nr == bytes && api->fs_write(dst, api->iobuf, bytes) == 0;
                                    break;
                                }
                                api->kfmt(shortname, sizeof shortname, "file%d.%s", ++serial, ext);
                                if (!desk_path(dst, shortname, sizeof dst)) { dst[0] = 0; break; }
                            }
                            if (!copied) ok = 0;
                        }
                    }
                }
                if (!more) break;
                p = end + 1;
            }
            if (ok && api->fs_rename(DESK_LST, "desktop.old") != 0) ok = 0;
        }
        if (!ok) api->klog("desktop: legacy migration incomplete; original files preserved\n");
    }
    lst_touch(); lst_sync();
}

#define CELLW  76
#define CELLH  56
#define ICONX  8
#define ICONY  26

static char desk_msg[64];
static u32  desk_msg_t;

static void say(const char *s)
{
    api->strlcpy(desk_msg, s, sizeof desk_msg);
    desk_msg_t = *api->ticks;
    api->gui_dirty();
}

enum { DQ_OPEN, DQ_TRANSFER, DQ_PASTE, DQ_RENAME, DQ_DELETE, DQ_NEW, DQ_INSTALL };
#define DQ_MAX 8
typedef struct { int kind,mode; u32 serial; char name[FS_NAMELEN],target[FS_NAMELEN]; char *data; } DeskJob;
static DeskJob desk_queue[DQ_MAX];
static int desk_qhead,desk_qcount,desk_active,desk_timer=-1;
static void desk_poll(void *ctx);
static u32 desk_lock(void){u32 f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;}
static void desk_unlock(u32 f){__asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");}
static int desk_enqueue(int kind,const char *name,const char *target,const char *data,int mode,u32 serial)
{
    DeskJob job;api->memset(&job,0,sizeof job);job.kind=kind;job.mode=mode;job.serial=serial;
    if((name&&api->strlen(name)>=sizeof job.name)||(target&&api->strlen(target)>=sizeof job.target)){
        say("Desktop request path is too long");return 0;
    }
    api->strlcpy(job.name,name?name:"",sizeof job.name);api->strlcpy(job.target,target?target:"",sizeof job.target);
    if(data){
        u32 n=api->strlen(data);if(n>=FT_LIST){say("Desktop request is too large");return 0;}
        job.data=api->kmalloc(n+1);if(!job.data){say("Not enough memory to queue desktop request");return 0;}
        api->memcpy(job.data,data,n+1);
    }
    u32 f=desk_lock();
    if(desk_qcount==DQ_MAX){desk_unlock(f);api->kfree(job.data);say("Desktop queue full; wait and try again");return 0;}
    if(desk_timer<0){desk_unlock(f);api->kfree(job.data);say("Desktop queue unavailable; try again");return 0;}
    desk_queue[(desk_qhead+desk_qcount)%DQ_MAX]=job;desk_qcount++;
    int waiting=desk_active||desk_qcount>1;desk_unlock(f);
    if(waiting)say("Desktop request queued; waiting for current work");
    return 1;
}

static u32 ren_serial;
static char ren_name[FS_NAMELEN];
static char ren_buf[FS_NAMELEN];
static int  ren_len, ren_car;
static int  ren_all;

static char sel[FS_NAMELEN];

static char dsel[DMAX][FS_NAMELEN];
static int  ndsel;
static u8   band;
static int  band_x0, band_y0, band_cx, band_cy;

static int dsel_has(const char *n)
{
    for (int i = 0; i < ndsel; i++)
        if (!api->strcmp(dsel[i], n)) return 1;
    return 0;
}
static void dsel_clear(void) { ndsel = 0; }
static void dsel_add(const char *n)
{
    if (!n || !n[0] || ndsel >= DMAX || dsel_has(n)) return;
    api->strlcpy(dsel[ndsel++], n, FS_NAMELEN);
}
static void dsel_remove(const char *n)
{
    for (int i = 0; i < ndsel; i++)
        if (!api->strcmp(dsel[i], n)) {
            for (; i < ndsel - 1; i++) api->strlcpy(dsel[i], dsel[i + 1], FS_NAMELEN);
            ndsel--;
            return;
        }
}
static void dsel_single(const char *n) { ndsel = 0; dsel_add(n); }
static int  sel_all_now(void) { return nvis > 0 && ndsel == nvis; }
static char lastp_name[FS_NAMELEN];
static u32  lastp_t;
static int  press_x, press_y;
static u8   dragging;

static int rows_per_col(void)
{
    int r = (*api->screen_h - 28 - ICONY) / CELLH;
    return r < 1 ? 1 : r;
}

static const char *icon_name(int n, int *ox, int *oy)
{
    if (n < 0 || n >= nvis) return 0;
    int rpc = rows_per_col();
    *ox = ICONX + (n / rpc) * CELLW;
    *oy = ICONY + (n % rpc) * CELLH;
    return names[vis[n]];
}

static const char *icon_at(int x, int y)
{
    int ox, oy;
    for (int n = 0; ; n++) {
        const char *nm = icon_name(n, &ox, &oy);
        if (!nm) return 0;
        if (x >= ox + 14 && x < ox + CELLW - 14 &&
            y >= oy && y < oy + CELLH - 4) return nm;
    }
}

static const char *name_ext(const char *n)
{
    const char *d = 0;
    for (const char *p = n; *p; p++)
        if (*p == '.') d = p + 1;
    return d ? d : "";
}

static int desk_isdir(const char *s){return api->fs_is_dir(s)||api->fs_dir_count(s)>0;}

static void icon_pic(int x, int y, const char *name)
{
    if(desk_isdir(name)){
        api->fill_rect(x+2,y+3,12,7,C_YELLOW);api->fill_rect(x+2,y+8,28,18,C_YELLOW);
        api->bevel(x+2,y+8,28,18,0);return;
    }
    if(archive_icon_name(api,name)){archive_icon(api,x,y,2);return;}
    const char *e = name_ext(name);
    if (!api->strcasecmp(e, "bmp")) {
        api->fill_rect(x + 2, y, 28, 24, C_CYAN);
        api->fill_rect(x + 2, y + 14, 28, 10, C_BGREEN);
        api->fill_rect(x + 20, y + 3, 6, 6, C_YELLOW);
        api->bevel(x + 2, y, 28, 24, 1);
        return;
    }
    if (!api->strcasecmp(e, "kx")) {
        api->fill_rect(x + 4, y, 24, 24, C_PURPLE);
        api->bevel(x + 4, y, 24, 24, 0);
        api->draw_text(x + 12, y + 4, "kx", C_WHITE);
        return;
    }
    api->fill_rect(x + 6, y, 20, 26, C_WHITE);
    api->bevel(x + 6, y, 20, 26, 1);
    u8 ink = !api->strcasecmp(e, "txt") ? C_NAVY : C_G0 + 3;
    for (int i = 0; i < 4; i++)
        api->hline(x + 9, y + 5 + i * 5, 14, ink);
}

static u8 *wp_pixels;
static char wp_loaded[64];

static void wp_free(void)
{
    if (wp_pixels) { api->kfree(wp_pixels); wp_pixels = 0; }
    wp_loaded[0] = 0;
}

static void wp_load(void)
{
    int sw = api->boot_info(BI_SCREEN_W), sh = api->boot_info(BI_SCREEN_H) - 24;
    if (sw <= 0 || sh <= 0) return;

    char path[64];
    api->strlcpy(path, api->cfg->wp_path, sizeof path);
    if (!wp_path_fix(path, sizeof path)) { wp_free(); return; }
    if (wp_pixels && !api->strcmp(path, wp_loaded)) return;
    wp_free();

    int n = api->fs_read(path, api->iobuf, api->iobuf_size);
    if (n <= 0) return;

    BmpHead h;
    const u8 *bm = api->iobuf;
    if (bmp_head(bm, (u32)n, &h) != 0) return;

    u8 map[256];
    if (h.bpp == 8) {
        for (int i = 0; i < 256; i++) {
            u32 e = h.paloff + (u32)i * 4;
            map[i] = (e + 4 <= (u32)n) ? api->palette_nearest(bm[e + 2], bm[e + 1], bm[e])
                                       : C_BLACK;
        }
    }

    u8 *px = api->kmalloc((u32)sw * (u32)sh);
    if (!px) return;
    api->memset(px, C_DESK, (u32)sw * (u32)sh);

    for (int y = 0; y < sh; y++) {
        int sy = bmp_src_row(&h, bmp_scale(y, sh, h.h));
        if (!bmp_row_ok(&h, sy, (u32)n)) continue;
        const u8 *row = bm + h.off + (u32)sy * h.rowsz;
        u8 *dst = px + (u32)y * (u32)sw;
        for (int x = 0; x < sw; x++) {
            int sx = bmp_scale(x, sw, h.w);
            dst[x] = h.bpp == 8 ? map[row[sx]]
                                : api->palette_nearest(row[sx * 3 + 2],
                                                       row[sx * 3 + 1],
                                                       row[sx * 3]);
        }
    }
    wp_pixels = px;
    api->strlcpy(wp_loaded, path, sizeof wp_loaded);
}

static void wp_refresh(void)
{
    if (wp_mode_norm(api->cfg->wp_mode, api->cfg->wp_path) == WP_BITMAP) wp_load();
    else wp_free();
    api->gui_dirty();
}

static void wp_event(const char *event, const char *data)
{
    (void)data;
    if (!api->strcmp(event, "wallpaper")) wp_refresh();
}

static void d_draw(void)
{
    const GdiOps *g = gdi_bind(api, 11);
    int sw = api->boot_info(BI_SCREEN_W), sh = api->boot_info(BI_SCREEN_H);

    switch (wp_mode_norm(api->cfg->wp_mode, api->cfg->wp_path)) {
    case WP_BITMAP:
        if (wp_pixels) {
            api->blit(0, 0, sw, sh - 24, wp_pixels, sw);
            break;
        }

    case WP_GRADIENT:

        if (g) {
            const u8 *ca = api->cfg->wp_ga, *cb = api->cfg->wp_gb;
            u32 a = GRGB(36, 80, 100), b = GRGB(16, 34, 56);
            if (wp_rgb_any(ca, 3) || wp_rgb_any(cb, 3)) {
                a = GRGB(ca[0], ca[1], ca[2]);
                b = GRGB(cb[0], cb[1], cb[2]);
            }
            g->set_dither(1);
            g->fill_gradient(0, 0, sw, sh - 24, a, b, 1);
            g->set_dither(0);
        }
        break;
    case WP_SOLID: {

        const u8 *c = api->cfg->wp_col;
        if (g) {
            g->set_dither(1);
            g->fill_gradient(0, 0, sw, sh - 24, GRGB(c[0], c[1], c[2]),
                             GRGB(c[0], c[1], c[2]), 1);
            g->set_dither(0);
        } else {
            api->fill_rect(0, 0, sw, sh - 24,
                           api->palette_nearest(c[0], c[1], c[2]));
        }
        break;
    }
    }
    lst_sync();
    int x, y;
    for (int n = 0; ; n++) {
        const char *nm = icon_name(n, &x, &y);
        if (!nm) break;
        int is_sel = dsel_has(nm);
        icon_pic(x + (CELLW - 32) / 2, y + 2, nm);
        if (ren_name[0] && !api->strcmp(ren_name, nm)) {

            int ew = CELLW + 40, ex = x + (CELLW - ew) / 2;
            if (ex < 1) ex = 1;

            if (ex + ew > *api->screen_w - 1) ex = *api->screen_w - 1 - ew;
            api->fill_rect(ex, y + 31, ew, 16, C_WHITE);
            api->rect(ex, y + 31, ew, 16, C_NAVY);
            if (ren_all && ren_len)
                api->fill_rect(ex + 2, y + 32, ren_len * 8, 14, C_NAVY);
            api->draw_text_clip(ex + 2, y + 32, ren_buf,
                                ren_all ? C_WHITE : C_BLACK, ew - 4);

            if (*api->gui_blink && !ren_all)
                api->fill_rect(ex + 2 + ren_car * 8, y + 32, 1, 14, C_BLACK);
            continue;
        }
        const char *label = desk_leaf(nm);
        int tw = (int)api->strlen(label) * 8;
        if (tw > CELLW - 4) tw = CELLW - 4;
        int tx = x + (CELLW - tw) / 2;
        if (is_sel) api->fill_rect(tx - 2, y + 32, tw + 4, 14, C_NAVY);
        else api->draw_text_clip(tx + 1, y + 33, label, C_BLACK, tw);
        api->draw_text_clip(tx, y + 32, label, C_WHITE, tw);
    }
    if (band) {
        int lx, hx, ly, hy;
        mq_norm(band_x0, band_cx, &lx, &hx);
        mq_norm(band_y0, band_cy, &ly, &hy);
        api->hline(lx, ly, hx - lx + 1, C_WHITE);
        api->hline(lx, hy, hx - lx + 1, C_WHITE);
        api->vline(lx, ly, hy - ly + 1, C_WHITE);
        api->vline(hx, ly, hy - ly + 1, C_WHITE);
    }
    if (desk_msg[0]) {
        if ((u32)(*api->ticks - desk_msg_t) > 400) desk_msg[0] = 0;
        else {
            int by = *api->screen_h - 28 - 20;
            api->draw_text(9, by + 1, desk_msg, C_BLACK);
            api->draw_text(8, by, desk_msg, C_YELLOW);
        }
    }
}

static void icon_open_now(const char *name)
{
    char nm[FS_NAMELEN];
    api->strlcpy(nm, name, sizeof nm);
    if(desk_isdir(nm)){
        if(api->kext_load("sys/files.kx")){say("Files could not be loaded");return;}
        api->broadcast("folder.open",nm);return;
    }
    api->buffer_lock();
    api->busy_set("Opening", nm, -1);
    FileData file;
    int r=fo_load(api,0,nm,0,&file,0);
    if(r)say(fo_error(r));
    else if (api->open_with(nm, 0, file.data, (int)file.size) != 0)
        say("no app for this file");
    fo_release(api,&file);
    api->busy_end();
    api->buffer_unlock();
}

static void icon_open(const char *name)
{desk_enqueue(DQ_OPEN,name,0,0,0,0);}

static FtBatch transfers;
static char desk_specs[FT_LIST];
static void desktop_transfer_now(const char *list,int mode,const char *folder)
{
    char dest[FS_NAMELEN],msg[64];api->strlcpy(dest,folder,sizeof dest);
    ft_batch(api,list,0,dest,mode,&transfers);ft_message(api,&transfers,msg,sizeof msg);
    say(msg);lst_touch();ft_report(api,&transfers);
}
static void desktop_transfer(const char *list,int mode,const char *folder)
{desk_enqueue(DQ_TRANSFER,folder,0,list,mode,0);}
static void place_on_desk(const char *spec,int move)
{desktop_transfer(spec,move?FT_DRAG:FT_COPY,"desktop");}
static void desktop_paste(const char *folder)
{
    int mode;
    if(!ft_clip_get(api,desk_specs,&mode)){say("No files on clipboard");return;}
    desk_enqueue(DQ_PASTE,folder,0,desk_specs,mode,0);
}
static void desktop_copy(int cut)
{
    int used=0;desk_specs[0]=0;
    for(int i=0;i<ndsel;i++){
        char spec[FS_NAMELEN+4];api->kfmt(spec,sizeof spec,"a:%s%s",dsel[i],desk_isdir(dsel[i])?"/":"");
        int n=api->strlen(spec);if(used+n+2>=FT_LIST){say("Selection is too large for clipboard");return;}
        if(used)desk_specs[used++]='\n';api->memcpy(desk_specs+used,spec,n+1);used+=n;
    }
    if(!used){say("Select files first");return;}
    if(api->clip_set(cut?"file.cut":"file",desk_specs,used+1)){say("Clipboard could not be updated");return;}
    char msg[64];api->kfmt(msg,sizeof msg,"%d items ready to %s",ndsel,cut?"move":"copy");say(msg);
}

static void show_props(const char *name)
{
    for (int i=0;i<FS_NFILES;i++) {
        FsEnt *e=api->fs_slot(i);
        if (e && e->used && !api->strcmp(e->name,name)) {
            fp_show(desk_leaf(e->name),api->ext_type(e->name),e->size,e->mtime,
                    "A:/desktop",!!(e->attr & FS_ATTR_DIR));
            return;
        }
    }
    if(desk_isdir(name))fp_show(desk_leaf(name),"Folder",0,0,"A:/desktop",1);
}

static int ends_ku(const char *s)
{
    u32 l = api->strlen(s);
    return l > 3 && !api->strcasecmp(s + l - 3, ".ku");
}

enum { DA_OPEN, DA_CUT, DA_COPY, DA_PASTE, DA_DUP, DA_PROPS, DA_DELETE,
       DA_INSTALL, DA_RENAME };
static u8 d_mcode[10];

static void ren_begin(const char *nm)
{
    ren_serial++;
    api->strlcpy(ren_name, nm, sizeof ren_name);
    api->strlcpy(ren_buf, desk_leaf(nm), sizeof ren_buf);
    ren_len = (int)api->strlen(ren_buf);
    ren_car = ren_len;
    ren_all = 1;
    api->gui_dirty();
}

static void ren_cancel(void)
{
    ren_serial++;
    ren_name[0] = 0;
    ren_buf[0] = 0;
    ren_len = ren_car = ren_all = 0;
    api->gui_dirty();
}

static void ren_commit(void)
{
    if (!ren_name[0]) return;
    if (!ren_len) { say("a name cannot be empty"); ren_cancel(); return; }
    char dst[FS_NAMELEN];
    if (!desk_path(dst, ren_buf, sizeof dst)) { say("Use 1-15 characters, without slashes"); return; }
    if (api->strcmp(dst, ren_name)) {
        if (api->fs_exists(dst)) { say("That name is already used"); return; }
        char source[FS_NAMELEN];api->strlcpy(source,ren_name,sizeof source);
        ren_cancel();u32 serial=ren_serial;
        if(!desk_enqueue(DQ_RENAME,source,dst,0,0,serial)&&!ren_name[0]&&ren_serial==serial){
            ren_begin(source);api->strlcpy(ren_buf,desk_leaf(dst),sizeof ren_buf);ren_len=ren_car=api->strlen(ren_buf);
        }
        return;
    }
    ren_cancel();
}

static int d_key(int k)
{
    int ctrl = api->kbd_mods() & 2;

    int c = ctrl ? kb_unctrl(k, 1) : k;
    if (!ren_name[0]) {
        if (ctrl && c == 'a') {
            dsel_clear();
            for (int i = 0; i < nvis; i++) dsel_add(names[vis[i]]);
            if (nvis) api->strlcpy(sel, names[vis[nvis - 1]], sizeof sel);
            api->gui_dirty();
            return 1;
        }
        if(ctrl&&c=='c'){desktop_copy(0);return 1;}
        if(ctrl&&c=='x'){desktop_copy(1);return 1;}
        if(ctrl&&c=='v'){desktop_paste("desktop");return 1;}
        if(ctrl&&c=='r'&&sel[0]){ren_begin(sel);return 1;}
        if((k=='\n'||k=='\r')&&sel[0]){icon_open(sel);return 1;}
        return 0;
    }
    if (k == '\n')      { ren_commit(); return 1; }
    if (k == 27)        { ren_cancel(); return 1; }
    if (ctrl && c == 'a') { ren_all = 1; api->gui_dirty(); return 1; }

    int printable = k >= 32 && k < 127 && k != '/';
    TextField t = { ren_buf, 16, ren_len, ren_car, ren_all };
    tf_key(&t, k, printable);
    ren_len = t.len; ren_car = t.caret; ren_all = t.all;
    api->gui_dirty();
    return 1;
}

static char del_list[FT_LIST];
static void del_confirmed(int result, void *ctx)
{
    (void)ctx;
    if(result==MBR_YES&&del_list[0])desk_enqueue(DQ_DELETE,0,0,del_list,0,0);
    del_list[0]=0;
}

static void icon_pick(int idx, void *ctx)
{
    (void)ctx;
    if (!sel[0] || idx < 0 || idx >= (int)sizeof d_mcode) return;
    char spec[FS_NAMELEN + 4];
    api->kfmt(spec, sizeof spec, "a:%s%s", sel,desk_isdir(sel)?"/":"");
    switch (d_mcode[idx]) {
    case DA_OPEN: icon_open(sel); break;
    case DA_CUT: desktop_copy(1); break;
    case DA_COPY: desktop_copy(0); break;
    case DA_PASTE: desktop_paste(desk_isdir(sel)?sel:"desktop"); break;
    case DA_DUP: place_on_desk(spec, 0); break;
    case DA_PROPS: show_props(sel); break;
    case DA_RENAME: ren_begin(sel); break;
    case DA_DELETE: {
        char q[160];int used=0,del_all=ndsel>1;del_list[0]=0;
        if(del_all){
            for(int i=0;i<ndsel;i++){
                int n=api->strlen(dsel[i]);if(used+n+2>=(int)sizeof del_list){say("Selection is too large to queue");return;}
                if(used)del_list[used++]='\n';api->memcpy(del_list+used,dsel[i],n+1);used+=n;
            }
        }else api->strlcpy(del_list,sel,sizeof del_list);
        if (del_all)
            api->kfmt(q, sizeof q,
                      sel_all_now()
                        ? "Delete all %d desktop files from the floppy? This "
                          "erases the files themselves."
                        : "Delete these %d files from the floppy? This erases "
                          "the files themselves.", ndsel);
        else if (sh_is_system_file(sel))
            api->kfmt(q, sizeof q,
                      "%s is a SYSTEM extension. Deleting it removes that "
                      "feature from FLOPNIX at the next boot. Really delete?",
                      sel);
        else
            api->kfmt(q, sizeof q,
                      "Delete %s from Desktop? This cannot be undone.", desk_leaf(sel));
        api->msgbox("Delete file", q, MB_YESNO, del_confirmed, 0);
        break;
    }
    case DA_INSTALL: desk_enqueue(DQ_INSTALL,sel,0,0,0,0); break;
    }
    api->gui_dirty();
}

static void desk_pick_now(int idx, void *ctx)
{
    (void)ctx;
    if (idx == 0) {
        desktop_paste("desktop");
    } else if (idx == 1) {
        char nm[FS_NAMELEN];
        for (int i = 0; i < 10; i++) {
            if (i) api->kfmt(nm, sizeof nm, "desktop/new%d.txt", i + 1);
            else   api->strlcpy(nm, "desktop/new.txt", sizeof nm);
            if (!api->fs_exists(nm)) {
                if (api->fs_write(nm, (const u8 *)"\n", 1) == 0)
                    lst_add(nm);
                break;
            }
        }
    } else if(idx==2){
        char nm[FS_NAMELEN];for(int n=1;n<100;n++){
            api->kfmt(nm,sizeof nm,"desktop/Folder%d",n);
            if(!api->fs_exists(nm)&&!api->fs_dir_count(nm)){
                if(!api->fs_mkdir(nm)){lst_touch();dsel_single(nm);api->strlcpy(sel,nm,sizeof sel);ren_begin(nm);}else say("Could not create folder");break;
            }
        }
    } else if (idx == 3) lst_load();
    api->gui_dirty();
}

static void desk_pick(int idx,void *ctx)
{
    (void)ctx;if(idx==0)desktop_paste("desktop");
    else if(idx>=1&&idx<=3)desk_enqueue(DQ_NEW,0,0,0,idx,0);
}

static void d_mouse(int x, int y, int ev)
{
    if (ev == EV_PRESS || ev == EV_RPRESS) {

        const char *hit = icon_at(x, y);
        if (ren_name[0] && (!hit || api->strcmp(hit, ren_name))) ren_commit();
    }
    if (ev == EV_PRESS) {
        const char *e = icon_at(x, y);
        int ctrl = (api->kbd_mods() & 2) != 0;
        press_x = x;
        press_y = y;
        dragging = 0;
        band = 0;
        if (e && ctrl) {
            if (dsel_has(e)) dsel_remove(e);
            else             dsel_add(e);
            api->strlcpy(sel, e, sizeof sel);
            api->strlcpy(lastp_name, e, sizeof lastp_name);
            api->gui_dirty();
            return;
        }

        if (e && !dsel_has(e)) dsel_single(e);
        if (!e) {
            dsel_clear();
            band = 1;
            band_x0 = band_cx = x;
            band_y0 = band_cy = y;
        }
        if (e) {
            if (!api->strcmp(e, lastp_name) && api->dclick()) {

                lastp_name[0] = 0;
                icon_open(e);
                return;
            }
            api->strlcpy(sel, e, sizeof sel);
            api->strlcpy(lastp_name, e, sizeof lastp_name);
            lastp_t = *api->ticks;
        } else {
            sel[0] = 0;
            lastp_name[0] = 0;
        }
    } else if (ev == EV_DRAG) {
        if (band) {
            band_cx = x;
            band_cy = y;
            dsel_clear();
            int ix, iy;
            for (int n = 0; ; n++) {
                const char *nm = icon_name(n, &ix, &iy);
                if (!nm) break;
                if (mq_hit(band_x0, band_y0, band_cx, band_cy,
                           ix, iy, CELLW, CELLH))
                    dsel_add(nm);
            }
            api->gui_dirty();
            return;
        }
        int dx = x - press_x, dy = y - press_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (!dragging && sel[0] && dx + dy > 8) {
            char spec[FS_NAMELEN + 4];
            api->kfmt(spec, sizeof spec, "a:%s%s", sel,desk_isdir(sel)?"/":"");
            static char specs[4096];int used=0;
            for(int i=0;i<ndsel;i++){
                if(used+FS_NAMELEN+5>=(int)sizeof specs)break;
                api->kfmt(specs+used,sizeof specs-used,"%sa:%s%s",used?"\n":"",dsel[i],desk_isdir(dsel[i])?"/":"");
                used+=(int)api->strlen(specs+used);
            }
            api->drag_start("file", ndsel>1?specs:spec);
            dragging = 1;
        }
    } else if (ev == EV_RELEASE) {
        dragging = 0;
        if (band) { band = 0; api->gui_dirty(); }
    } else if (ev == EV_RPRESS) {
        const char *e = icon_at(x, y);
        if (e) {

            if (!dsel_has(e)) dsel_single(e);
            api->strlcpy(sel, e, sizeof sel);
            const char *items[10];
            int n = 0;
            items[n] = "Open"; d_mcode[n++] = DA_OPEN;
            if (ends_ku(sel))
                { items[n] = "Install kernel"; d_mcode[n++] = DA_INSTALL; }
            items[n] = "Rename"; d_mcode[n++] = DA_RENAME;
            items[n] = "Cut"; d_mcode[n++] = DA_CUT;
            items[n] = "Copy"; d_mcode[n++] = DA_COPY;
            items[n] = "Paste"; d_mcode[n++] = DA_PASTE;
            items[n] = "Duplicate"; d_mcode[n++] = DA_DUP;
            items[n] = "Properties"; d_mcode[n++] = DA_PROPS;
            items[n] = "Delete"; d_mcode[n++] = DA_DELETE;
            api->menu_show(x, y, items, n, icon_pick, 0);
        } else {
            sel[0] = 0;
            static const char *const it[] = { "Paste", "New text file", "New folder", "Refresh" };
            api->menu_show(x, y, it, 4, desk_pick, 0);
        }
    }
}

static void d_drop(int x, int y, const char *type, const char *data)
{
    char folder[FS_NAMELEN];api->strlcpy(folder,"desktop",sizeof folder);
    const char *target=icon_at(x,y);if(target&&desk_isdir(target))api->strlcpy(folder,target,sizeof folder);
    if (api->strcmp(type, "file")) return;
    desktop_transfer(data,FT_DRAG,folder);
}

static void desk_rename(const DeskJob *job)
{
    const char *err=0;
    if(api->fs_exists(job->target))err="That name is already used";
    else if(desk_isdir(job->name)?api->fs_rename_dir(job->name,job->target):api->fs_rename(job->name,job->target))
        err="Could not rename: check contents and name length";
    if(err){
        if(!ren_name[0]&&ren_serial==job->serial){
            ren_begin(job->name);api->strlcpy(ren_buf,desk_leaf(job->target),sizeof ren_buf);
            ren_len=ren_car=api->strlen(ren_buf);ren_all=1;
        }
        say(err);return;
    }
    if(!api->strcmp(sel,job->name))api->strlcpy(sel,job->target,sizeof sel);
    for(int i=0;i<ndsel;i++)if(!api->strcmp(dsel[i],job->name))api->strlcpy(dsel[i],job->target,FS_NAMELEN);
    lst_touch();api->gui_dirty();
}
static void desk_delete(char *list)
{
    for(char *p=list;p&&*p;){
        char *end=p;while(*end&&*end!='\n')end++;char more=*end;*end=0;
        if(api->fs_dir_count(p))say("Empty folders before deleting them");
        else if(api->fs_delete(p))say("Could not delete file; try again");
        else{lst_del(p);dsel_remove(p);if(!api->strcmp(sel,p))sel[0]=0;}
        if(!more)break;p=end+1;
    }
    lst_touch();api->gui_dirty();
}
static void desk_poll(void *ctx)
{
    (void)ctx;u32 f=desk_lock();
    if(desk_active||!desk_qcount){desk_unlock(f);return;}
    DeskJob job=desk_queue[desk_qhead];desk_qhead=(desk_qhead+1)%DQ_MAX;desk_qcount--;desk_active=1;
    desk_unlock(f);
    switch(job.kind){
    case DQ_OPEN:icon_open_now(job.name);break;
    case DQ_TRANSFER:case DQ_PASTE:
        desktop_transfer_now(job.data,job.mode,job.name);
        if(job.kind==DQ_PASTE&&job.mode==FT_MOVE)ft_clip_finish(api,job.data,&transfers);
        break;
    case DQ_RENAME:desk_rename(&job);break;
    case DQ_DELETE:desk_delete(job.data);break;
    case DQ_NEW:desk_pick_now(job.mode,0);break;
    case DQ_INSTALL:{char err[48];if(api->kernel_update(job.name,err,sizeof err))say(err);break;}
    }
    api->kfree(job.data);f=desk_lock();desk_active=0;
    desk_unlock(f);
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "Desktop"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    desk_timer=k->timer_add(2,desk_poll,0);
    if(desk_timer<0)return 1;
    fp_register();

    static const DesktopOps ops = {
        .draw      = d_draw,
        .mouse     = d_mouse,
        .drop      = d_drop,
        .key       = d_key,
    };
    k->register_desktop(&ops);

    lst_load();
    lst_sync();

    wp_refresh();
    k->on_event(wp_event);
    return 0;
}
