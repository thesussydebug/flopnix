/* Connects kernel calls to loaded extension services. */
#include "os.h"

static const FatOps *fops;
static const NetOps *nops;
static const DesktopOps *dops;
static const DialogOps *dlg;

static int fat_owner = -1, net_owner = -1, desk_owner = -1, dlg_owner = -1;

void register_fat(const FatOps *ops) { fops = ops; fat_owner = kext_loading(); }
void register_net(const NetOps *ops) { nops = ops; net_owner = kext_loading(); }
void register_desktop(const DesktopOps *ops) { dops = ops; desk_owner = kext_loading(); }
void register_dialogs(const DialogOps *ops) { dlg = ops; dlg_owner = kext_loading(); }

static Mutex fat_mx = MUTEX_INIT;
#define FATLOCK(call, dflt)      FATLOCKV(int, use_fat(), call, dflt)
#define FATLOCKT(call, dflt)     FATLOCKV(int, fat_tail_ok(), call, dflt)
#define FATLOCKV(T, ok, call, dflt)     T _r = (T)(dflt); mtx_lock(&fat_mx); if (ok) _r = (call);     mtx_unlock(&fat_mx); return _r;

static int use_fat(void)  { if (!fops) return 0; kext_enter(fat_owner);  return 1; }
static int use_net(void)  { if (!nops) return 0; kext_enter(net_owner);  return 1; }
static int use_desk(void) { if (!dops) return 0; kext_enter(desk_owner); return 1; }
static int use_dlg(void)  { if (!dlg)  return 0; kext_enter(dlg_owner);  return 1; }

static void desk_lost(const char *what)
{
    dops = 0;
    klog("desktop ");
    klog(what);
    klog(" faulted - desktop environment dropped\n");
}
void desk_draw(void)
{
    if (use_desk() && dops->draw) {
        void (*f)(void) = dops->draw;
        FAULT_GUARD(f(), { if (!fault_fallback[thr_self]) desk_lost("draw"); });
    }
}
void desk_mouse(int x, int y, int ev)
{
    if (use_desk() && dops->mouse) {
        void (*f)(int, int, int) = dops->mouse;
        FAULT_GUARD(f(x, y, ev), desk_lost("mouse"));
    }
}
void desk_drop(int x, int y, const char *type, const char *data)
{
    if (use_desk() && dops->drop) {
        void (*f)(int, int, const char *, const char *) = dops->drop;
        FAULT_GUARD(f(x, y, type, data), desk_lost("drop"));
    }
}

int desk_key(int k)
{
    if (!use_desk() || !dops->key) return 0;
    int (*f)(int) = dops->key;
    volatile int r = 0;
    FAULT_GUARD(r = f(k), { r = 0; desk_lost("key"); });
    return r;
}

#define DEFER_CB(NAME, ARGT)                                          \
    static struct { void (*cb)(ARGT, void *); void *ctx; int owner; } NAME; \
    static void NAME##_tramp(ARGT a, void *c) {                       \
        (void)c;                                                      \
        kext_enter(NAME.owner);                                       \
        if (NAME.cb) FAULT_GUARD(NAME.cb(a, NAME.ctx), {});           \
    }
DEFER_CB(g_msg,   int)
DEFER_CB(g_menu,  int)
DEFER_CB(g_fpick, const char *)
DEFER_CB(g_fsave, const char *)

int clip_set(const char *type, const void *data, u32 n)
{
    if (use_desk() && dops->clip_set) return dops->clip_set(type, data, n);
    return clip_set_native(type, data, n);
}
int clip_get(const char *type, void *buf, u32 max)
{
    if (use_desk() && dops->clip_get) return dops->clip_get(type, buf, max);
    return clip_get_native(type, buf, max);
}
const char *clip_type(void)
{
    if (use_desk() && dops->clip_type) return dops->clip_type();
    return clip_type_native();
}
void menu_show(int x, int y, const char *const *items, int n,
               void (*pick)(int idx, void *ctx), void *ctx)
{

    g_menu.cb = pick; g_menu.ctx = ctx; g_menu.owner = kext_current();
    if (use_desk() && dops->menu_show)
        dops->menu_show(x, y, items, n, g_menu_tramp, 0);
    else menu_show_native(x, y, items, n, g_menu_tramp, 0);
}

int clip_set_text(const char *s) { return clip_set("text", s, strlen(s) + 1); }
int clip_get_text(char *buf, int cap)
{
    int n = clip_get("text", buf, cap - 1 > 0 ? (u32)(cap - 1) : 0);
    if (n < 0) { if (cap) buf[0] = 0; return -1; }
    if (n >= cap) n = cap - 1;
    buf[n] = 0;
    return n;
}

void msgbox(const char *title, const char *text, int buttons,
            void (*cb)(int result, void *ctx), void *ctx)
{
    if (use_dlg() && dlg->msgbox) {
        g_msg.cb = cb; g_msg.ctx = ctx; g_msg.owner = kext_current();
        dlg->msgbox(title, text, buttons, g_msg_tramp, 0);
    }
    else if (cb) cb(MBR_OK, ctx);
}
void notify(const char *text) { if (use_dlg() && dlg->notify) dlg->notify(text); }
void file_picker(const char *title, const char *ext, int dirs_only,
                 void (*cb)(const char *path, void *ctx), void *ctx)
{
    if (use_dlg() && dlg->file_picker) {
        g_fpick.cb = cb; g_fpick.ctx = ctx; g_fpick.owner = kext_current();
        dlg->file_picker(title, ext, dirs_only, g_fpick_tramp, 0);
    }
    else if (cb) cb(0, ctx);
}
void file_save(const char *title, const char *ext, const char *defname,
               void (*cb)(const char *path, void *ctx), void *ctx)
{
    if (use_dlg() && dlg->file_save) {
        g_fsave.cb = cb; g_fsave.ctx = ctx; g_fsave.owner = kext_current();
        dlg->file_save(title, ext, defname, g_fsave_tramp, 0);
    }
    else if (cb) cb(0, ctx);
}
void progress_open(const char *title) { if (use_dlg() && dlg->progress_open) dlg->progress_open(title); }
void progress_set(int pct, const char *label) { if (use_dlg() && dlg->progress_set) dlg->progress_set(pct, label); }
void progress_close(void) { if (use_dlg() && dlg->progress_close) dlg->progress_close(); }

int fat_mount(void)        { FATLOCK(fops->mount(), 0) }
const char *fat_label(void){ FATLOCKV(const char *, use_fat(), fops->label(), "none") }
int fat_writable(void)     { FATLOCK(fops->writable(), 0) }
u32 fat_total_kb(void)     { FATLOCKV(u32, use_fat(), fops->total_kb(), 0) }
u32 fat_free_kb(void)      { FATLOCKV(u32, use_fat(), fops->free_kb(), 0) }

int fat_list(const char *path, FatEnt *out, int max)
{
    FATLOCK(fops->list(path, out, max), -1)
}
int fat_read(const char *path, u8 *buf, u32 max)
{
    FATLOCK(fops->read(path, buf, max), -1)
}
int fat_write(const char *path, const u8 *buf, u32 size)
{
    FATLOCK(fops->write(path, buf, size), -1)
}
int fat_delete(const char *path)
{
    FATLOCK(fops->del(path), -1)
}

int fat_append(const char *path, const u8 *buf, u32 size)
{
    FATLOCKV(int, use_fat() && fops->abi == FAT_ABI,
             fops->append(path, buf, size), -1)
}

static int fat_tail_ok(void) { return use_fat() && fops->abi == FAT_ABI; }
int fat_mkdir(const char *path)
{
    FATLOCKT(fops->mkdir(path), -1)
}
int fat_rename(const char *path, const char *newname)
{
    FATLOCKT(fops->rename(path, newname), -1)
}
int fat_can_mkdir(void) { return fat_tail_ok(); }
int fat_rmdir(const char *path)
{
    FATLOCKT(fops->rmdir(path), -1)
}

int fat_exists(const char *path)
{
    FATLOCKT(fops->exists(path), 0)
}

void net_poll(void)              { if (use_net()) nops->poll(); }
int  net_up(void)                { return use_net() ? nops->up() : 0; }
int  net_dhcp(u32 timeout)       { return use_net() ? nops->dhcp(timeout) : 0; }
int  net_ping(u32 dst, u32 t)    { return use_net() ? nops->ping(dst, t) : -2; }
u32  net_get(int what)           { return use_net() ? nops->get(what) : 0; }
void net_set(int what, u32 v)    { if (use_net()) nops->set(what, v); }

static int net_has_v11(void)     { return use_net() && nops->v11 == NET_ABI_V11; }
u32  net_dns(const char *n, u32 t)
{
    return net_has_v11() ? nops->dns(n, t) : 0;
}
int  net_http_get(u32 ip, u16 port, const char *host, const char *path,
                  int (*sink)(const u8 *, int, void *), void *ctx, u32 t)
{
    return net_has_v11() ? nops->http_get(ip, port, host, path, sink, ctx, t)
                         : -2;
}
static int net_has_v12(void)     { return use_net() && nops->v12 == NET_ABI_V12; }
u32  net_sntp(u32 ip, u32 t)
{
    return net_has_v12() ? nops->sntp(ip, t) : 0;
}

const u8 *net_mac_get(void)
{
    static const u8 zero[6];
    return use_net() ? nops->mac() : zero;
}

int services_kext_busy(int owner)
{
    return (fops && fat_owner==owner) || (nops && net_owner==owner) ||
           (dops && desk_owner==owner) || (dlg && dlg_owner==owner);
}
