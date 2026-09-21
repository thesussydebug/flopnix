/* Connects kernel calls to loaded extension services. */
#include "os.h"
#include "debug.h"

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

#define FATFILE(ok,call,write) FATFILEV(ok,call,write,-1)
#define FATFILEV(ok,call,write,dflt) \
    int r=(dflt);mtx_lock(&fat_mx);const char *old=debug_path(1,path); \
    if(ok)r=(call); \
    debug_done(1|((write)?2:0),old,r); \
    mtx_unlock(&fat_mx);return r;

static int use_fat(void)  { return fops != 0; }
static int use_net(void)  { return nops != 0; }
static int use_desk(void) { return dops != 0; }
static int use_dlg(void)  { return dlg != 0; }

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
        int resident=kext_current();kext_enter(desk_owner);
        void (*f)(void) = dops->draw;
        FAULT_GUARD(f(), { if (!fault_fallback[thr_self]) desk_lost("draw"); });
        kext_enter(resident);
    }
}
void desk_mouse(int x, int y, int ev)
{
    if (use_desk() && dops->mouse) {
        int resident=kext_current();kext_enter(desk_owner);
        void (*f)(int, int, int) = dops->mouse;
        FAULT_GUARD(f(x, y, ev), desk_lost("mouse"));
        kext_enter(resident);
    }
}
void desk_drop(int x, int y, const char *type, const char *data)
{
    if (use_desk() && dops->drop) {
        int resident=kext_current();kext_enter(desk_owner);
        void (*f)(int, int, const char *, const char *) = dops->drop;
        FAULT_GUARD(f(x, y, type, data), desk_lost("drop"));
        kext_enter(resident);
    }
}

int desk_key(int k)
{
    if (!use_desk() || !dops->key) return 0;
    int resident=kext_current();kext_enter(desk_owner);
    int (*f)(int) = dops->key;
    volatile int r = 0;
    FAULT_GUARD(r = f(k), { r = 0; desk_lost("key"); });
    kext_enter(resident);
    return r;
}

static int dialog_active,dialog_owner=-1;
static int dialog_begin(int owner)
{
    for(;;){
        u32 f=irq_save();
        if(!dialog_active&&!overlay_modal()){dialog_active=1;dialog_owner=owner;irq_restore(f);return 1;}
        irq_restore(f);
        if(app_cancel_pending())return 0;
        gui_pump();thr_yield();
    }
}
static void dialog_end(void){dialog_active=0;dialog_owner=-1;}
static int callback_window(int owner)
{
    int w=app_current_window();
    return w>=0&&app_type_owner(wins[w].type)==owner?w:-1;
}

#define DEFER_CB(NAME, ARGT, PATH, GATED) \
    static struct {void (*cb)(ARGT,void *);void *ctx;int owner,win;} NAME; \
    static void NAME##_tramp(ARGT a,void *c){ \
        (void)c;void (*fn)(ARGT,void *)=NAME.cb;void *ctx=NAME.ctx;int owner=NAME.owner,win=NAME.win;NAME.cb=0; \
        int queued=fn?app_callback(owner,win,(void *)fn,ctx,PATH?0:(int)a,PATH?(const char *)a:0,PATH):0; \
        if(GATED)dialog_end(); \
        if(fn&&!queued){kext_enter(owner);FAULT_GUARD(fn(a,ctx),{});} \
    }
DEFER_CB(g_msg,int,0,1)
DEFER_CB(g_menu,int,0,0)
DEFER_CB(g_fpick,const char *,1,1)
DEFER_CB(g_fsave,const char *,1,1)

void services_dialog_drop(void)
{
    g_msg.cb=0;g_fpick.cb=0;g_fsave.cb=0;dialog_end();
}
int services_dialog_owner(void){return dialog_active?dialog_owner:-1;}

int clip_set(const char *type, const void *data, u32 n)
{
    if (dops && dops->clip_set && use_desk()) return dops->clip_set(type, data, n);
    return clip_set_native(type, data, n);
}
int clip_get(const char *type, void *buf, u32 max)
{
    if (dops && dops->clip_get && use_desk()) return dops->clip_get(type, buf, max);
    return clip_get_native(type, buf, max);
}
const char *clip_type(void)
{
    if (dops && dops->clip_type && use_desk()) return dops->clip_type();
    return clip_type_native();
}
void menu_show(int x, int y, const char *const *items, int n,
               void (*pick)(int idx, void *ctx), void *ctx)
{

    g_menu.cb = pick; g_menu.ctx = ctx; g_menu.owner = kext_current(); g_menu.win=callback_window(g_menu.owner);
    if (use_desk() && dops->menu_show)
        dops->menu_show(x, y, items, n, g_menu_tramp, 0);
    else menu_show_native(x, y, items, n, g_menu_tramp, 0);
}

int clip_set_text(const char *s) { return clip_set("text", s, strlen(s) + 1); }
int clip_get_text(char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    int n = clip_get("text", buf, cap - 1 > 0 ? (u32)(cap - 1) : 0);
    if (n < 0) { if (cap) buf[0] = 0; return -1; }
    if (n >= cap) n = cap - 1;
    buf[n] = 0;
    return n;
}

void msgbox(const char *title, const char *text, int buttons,
            void (*cb)(int result, void *ctx), void *ctx)
{
    int owner=kext_current();
    if (dlg && dlg->msgbox) {
        if(!dialog_begin(owner)){if(cb)cb(MBR_CANCEL,ctx);return;}
        use_dlg();g_msg.cb = cb; g_msg.ctx = ctx; g_msg.owner = owner; g_msg.win=callback_window(owner);
        dlg->msgbox(title, text, buttons, g_msg_tramp, 0);
    }
    else if (cb) cb(MBR_OK, ctx);
}
void notify(const char *text) { if(dialog_active||overlay_modal()){fault_show_banner(text);return;} if (use_dlg() && dlg->notify) dlg->notify(text); }
void file_picker(const char *title, const char *ext, int dirs_only,
                 void (*cb)(const char *path, void *ctx), void *ctx)
{
    int owner=kext_current();
    if (dlg && dlg->file_picker) {
        if(!dialog_begin(owner)){if(cb)cb(0,ctx);return;}
        use_dlg();g_fpick.cb = cb; g_fpick.ctx = ctx; g_fpick.owner = owner; g_fpick.win=callback_window(owner);
        dlg->file_picker(title, ext, dirs_only, g_fpick_tramp, 0);
    }
    else if (cb) cb(0, ctx);
}
void file_save(const char *title, const char *ext, const char *defname,
               void (*cb)(const char *path, void *ctx), void *ctx)
{
    int owner=kext_current();
    if (dlg && dlg->file_save) {
        if(!dialog_begin(owner)){if(cb)cb(0,ctx);return;}
        use_dlg();g_fsave.cb = cb; g_fsave.ctx = ctx; g_fsave.owner = owner; g_fsave.win=callback_window(owner);
        dlg->file_save(title, ext, defname, g_fsave_tramp, 0);
    }
    else if (cb) cb(0, ctx);
}
void progress_open(const char *title) { if(app_current_window()>=0){app_local_progress(title,0,0);return;} if (use_dlg() && dlg->progress_open) dlg->progress_open(title); }
void progress_set(int pct, const char *label) { if(app_current_window()>=0){app_local_progress("Working",label,pct<0?-1:pct>100?256:pct*256/100);gui_pump();return;} if (use_dlg() && dlg->progress_set) dlg->progress_set(pct, label); }
void progress_close(void) { if(app_current_window()>=0){app_local_progress(0,0,-1);return;} if (use_dlg() && dlg->progress_close) dlg->progress_close(); }

int fat_mount(void)        { FATLOCK(fops->mount(), 0) }
const char *fat_label(void){ FATLOCKV(const char *, use_fat(), fops->label(), "none") }
int fat_writable(void)     { FATLOCK(fops->writable(), 0) }
u32 fat_total_kb(void)     { FATLOCKV(u32, use_fat(), fops->total_kb(), 0) }
u32 fat_free_kb(void)      { FATLOCKV(u32, use_fat(), fops->free_kb(), 0) }

int fat_list(const char *path, FatEnt *out, int max)
{
    FATFILE(use_fat(),fops->list(path,out,max),0)
}
int fat_read(const char *path, u8 *buf, u32 max)
{
    FATFILE(use_fat(),fops->read(path,buf,max),0)
}
int fat_write(const char *path, const u8 *buf, u32 size)
{
    FATFILE(use_fat(),fops->write(path,buf,size),1)
}
int fat_delete(const char *path)
{
    FATFILE(use_fat(),fops->del(path),1)
}

int fat_append(const char *path, const u8 *buf, u32 size)
{
    FATFILE(use_fat()&&fops->abi==FAT_ABI,fops->append(path,buf,size),1)
}

static int fat_tail_ok(void) { return use_fat() && fops->abi == FAT_ABI; }
int fat_mkdir(const char *path)
{
    FATFILE(fat_tail_ok(),fops->mkdir(path),1)
}
int fat_rename(const char *path, const char *newname)
{
    FATFILE(fat_tail_ok(),fops->rename(path,newname),1)
}
int fat_can_mkdir(void) { return fat_tail_ok(); }
int fat_rmdir(const char *path)
{
    FATFILE(fat_tail_ok(),fops->rmdir(path),1)
}

int fat_exists(const char *path)
{
    FATFILEV(fat_tail_ok(),fops->exists(path),0,0)
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
void services_drop_owner(int owner)
{
    if(dialog_active&&dialog_owner==owner)services_dialog_drop();
    if(g_msg.owner==owner)g_msg.cb=0;
    if(g_menu.owner==owner)g_menu.cb=0;
    if(g_fpick.owner==owner)g_fpick.cb=0;
    if(g_fsave.owner==owner)g_fsave.cb=0;
}
