/* Shared kernel types and functions. */
#pragma once

#include "kapi.h"

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

#define OS_NAME    "FLOPNIX"
#define OS_VER     "0.8.8"
#ifndef OS_BUILD_DATE
#define OS_BUILD_DATE "unknown"
#endif
#define OS_RELEASE "flopnix " OS_VER " #1 i386 (built " OS_BUILD_DATE ")"

typedef struct __attribute__((packed)) {
    u16 w, h, pitch;
    u8  bpp, vbe;

    u32 lfb;
    u32 mem_kb;
    u8  diag;
    u8  stage;
    u16 vbe_mode;
} BootInfo;
#define BD_CFG_OK    0x01
#define BD_CFG_HUNG  0x02
#define BD_FONT_HUNG 0x04
#define BD_MEM_HUNG  0x08
#define BD_VGA_HUNG  0x10
#define BD_VBE_HUNG  0x20
#define BD_A20_HUNG  0x40
#define BD_ANY_HANG  0x80

#define BOOTINFO ((BootInfo *)0x7000)
#define FONT8x16 ((u8 *)0x5000)

#include "memlayout.inc"
#define KERNEL_MEMORY_LAYOUT 1
extern MemoryLayout memory;
#define DMABUF ((u8 *)MEM_DMA_BASE)
#define BACKBUF ((u8 *)memory.fb)

static inline void outb(u16 p, u8 v)  { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p) { u8 r;  __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline u16  inw(u16 p) { u16 r; __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline u32  inl(u16 p) { u32 r; __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
static inline void io_wait(void) { outb(0x80, 0); }

static inline u32 irq_save(void)
{
    u32 f;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(u32 f)
{
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");
}

int   k_atoi(const char *s);
const char *k_strchr(const char *s, int c);
const char *k_strstr(const char *hay, const char *needle);
int   k_toupper(int c);
int   k_tolower(int c);
void  ksort(void *base, int n, int size,
            int (*cmp)(const void *, const void *));
u32   krand(void);
void  krand_seed(u32 seed);
void  klog(const char *s);

void menu_show_native(int x, int y, const char *const *items, int n,
                      void (*pick)(int idx, void *ctx), void *ctx);
int  clip_set_native(const char *type, const void *data, u32 n);
int  clip_get_native(const char *type, void *buf, u32 max);
const char *clip_type_native(void);
int   klog_read(char *dst, int cap);
void  ktrace(const char *msg);
int   trace_read(char *dst, int cap);
u32   trace_seq_get(void);
u32   trace_held(void);
int   trace_slice_read(u32 off, char *dst, u32 count);
extern int trace_dirty;

int   b64_encode(const u8 *in, u32 n, char *out, int cap);
int   b64_decode(const char *in, u8 *out, int cap);
u32   crc32(const void *data, u32 n);
u32   hash_fnv(const void *data, u32 n);
void  uuid_gen(char *out);
const char *path_base(const char *p);
const char *path_ext(const char *p);
void  path_dir(const char *p, char *out, int cap);
void  path_join(char *out, int cap, const char *dir, const char *name);
void  date_fmt(u32 dt, const char *fmt, char *out, int cap);
void  ms_open(MemStream *s, u8 *buf, u32 cap);
int   ms_alloc(MemStream *s, u32 cap);
void  ms_free(MemStream *s);
int   ms_write(MemStream *s, const void *data, u32 n);
int   ms_read(MemStream *s, void *out, u32 n);
int   ms_seek(MemStream *s, u32 pos);

void  broadcast(const char *event, const char *data);
int   on_event(void (*fn)(const char *event, const char *data));
void  off_event(void (*fn)(const char *event, const char *data));

int   shell_history_count(void);
const char *shell_history(int i);
void *memcpy(void *d, const void *s, u32 n);
void *memmove(void *d, const void *s, u32 n);
void *memset(void *d, int c, u32 n);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, u32 n);
int   strcasecmp(const char *a, const char *b);
u32   strlen(const char *s);
void  strlcpy(char *d, const char *s, int cap);
void  kfmt(char *dst, int cap, const char *f, ...);
int   net_parse_ip(const char *s, u32 *out);
void  human_size(u32 bytes, char *buf, int cap);
void  human_size_kb(u32 kb, char *buf, int cap);

extern int SW, SH, SPITCH;
#define TBH 28
void gfx_init(void);
void fill_rect(int x, int y, int w, int h, u8 c);
void hline(int x, int y, int w, u8 c);
void vline(int x, int y, int h, u8 c);
void bevel(int x, int y, int w, int h, int sunken);
void panel(int x, int y, int w, int h, int sunken);
void draw_char(int x, int y, char ch, u8 fg);
void draw_text(int x, int y, const char *s, u8 fg);
void draw_text_clip(int x, int y, const char *s, u8 fg, int maxpx);
void draw_text_clip2(int x, int y, const char *s, u8 fg, int cx0, int cx1);
void draw_sbar(int x, int y, int len, int horiz, int total, int vis, int off);
int  sbar_from_pos(int len, int total, int vis, int pos);
void focus_rect(int x, int y, int w, int h);
void draw_cursor(int x, int y);
void blit(int x, int y, int w, int h, const u8 *src, int spitch);
void palette_rgb(int idx, u8 *r, u8 *g, u8 *b);
u8   palette_nearest(u8 r, u8 g, u8 b);
void set_clip(int x, int y, int w, int h);
void clear_clip(void);
int  surface_lock(u8 **px, int *pitch, int *w, int *h);
void surface_unlock(void);
void clip_rect_get(int *x, int *y, int *w, int *h);
void pixel(int x, int y, u8 c);
u8   getpixel(int x, int y);
void line(int x0, int y0, int x1, int y1, u8 c);
void rect(int x, int y, int w, int h, u8 c);
void circle(int cx, int cy, int r, u8 c);
void fill_circle(int cx, int cy, int r, u8 c);
void blit_key(int x, int y, int w, int h, const u8 *src, int spitch, u8 key);
void read_rect(int x, int y, int w, int h, u8 *dst, int dpitch);
void draw_text_scaled(int x, int y, const char *s, u8 fg, int sx, int sy);
const u8 *font_glyph(char ch);
void palette_set(int idx, u8 r, u8 g, u8 b);
void flip(void);
enum { EM_REPORT, EM_DOUBLE, EM_STALL, EM_NMI };
extern volatile u32 panic_active;
void emergency_init(void);
void emergency_video(u32 base,int w,int h,int pitch,int bank);
void emergency_heartbeat(void);
void emergency_watch_start(void);
void emergency_panic_begin(u32 vec,u32 err,u32 eip);
int emergency_irq(u32 vec,u32 eip);
__attribute__((noreturn)) void emergency_enter(u32 vec,u32 err,u32 eip,u32 cr2,u32 reason);
void idt_set_task_gate(int vec,u16 selector);
void panic(u32 vec, u32 err, u32 eip);
void fault_handle(const u32 *frame);
u32  cpu_now(void);

#define MAX_APPS 40
int  cpu_context(int type);
void cpu_thread_switch(int next);
int  app_type_owned(int owner);
void cpu_snapshot(void);
u32  cpu_usage(int t);
void loop_prof_fmt(char *out, int cap);
void loop_prof_reset(void);
void busy_set(const char *title, const char *msg, int frac256);
void busy_end(void);
#define THR_MAX 8
u32 thread_guard_mask(void);
u32 debug_flags(void);
void debug_event(u32 kind,const char *name,u32 a,u32 b,int result);
const char *debug_path(int usb,const char *path);
void debug_disk(int usb,int write,u32 lba,u32 count,int result);
void debug_draw(int win);
void debug_unwind(void);
int debug_done(int mode,const char *old,int result);
#include "sched.inc"
typedef struct { u32 ebx, esi, edi, ebp, esp, eip; } JmpBuf;
int  fj_set(JmpBuf *b) __attribute__((returns_twice));
void fj_long(JmpBuf *b, int val) __attribute__((noreturn));

extern volatile int fault_armed[THR_MAX];
extern JmpBuf fault_ctx[THR_MAX];
extern int thr_self;
extern u32 fault_vec, fault_err, fault_eip, fault_recoveries;
extern u32 fault_cr2;
extern u8 fault_fallback[THR_MAX];
unsigned kext_graphics_fault(u32 vec, u32 eip, u32 addr);
void fault_notice(void);

void paging_init(void);
void paging_map_kext(u32 phys, u32 len);

void paging_arena_protect(u32 lo, u32 hi, int writable);
u32  fdc_stat(int what);
typedef struct {u32 lba,ms,tries;u8 st[3];} FdcResult;
void fdc_result_get(FdcResult *result);
void fdc_result_restore(const FdcResult *result);
const char *shell_cwd_get(void);
int  shell_cwd_set(const char *dir);
void paging_unmap_kext(void);
int  paging_space_create(int slot, u32 phys, u32 len);
void paging_space_switch(int slot);
int  paging_space_current(void);
void paging_space_sync(void);
void paging_set_user(u32 va, u32 npages, int user);

void ring3_init(void);
int  ring3_active(void);
int  ring3_fault(u32 vec, u32 err);
u32  ring3_selftest(int what);
void idt_set_user_gate(int vec, u32 handler);
void paging_fb_write_combine(void);
void paging_flush_all(void);

int  kext_loading(void);
int  kext_owner_now(void);

int  kext_current(void);
void kext_enter(int k);
int  kext_fix_window(u32 eip, u32 cr2);
int  paging_active(void);
u32  page_fault_addr(void);
int  paging_mapped(u32 va);
int  paging_writable(u32 va);
int  mem_poke(u32 va, u8 val);
u32  paging_pages_mapped(void);
void paging_flush(void);
int fault_count(void);
const FaultRec *fault_get(int i);

#define FAULT_GUARD(body, recover) do {                               \
    int _ft = thr_self;                                               \
    JmpBuf _fsav = fault_ctx[_ft]; int _fasav = fault_armed[_ft];     \
    int _msnap = mtx_held_count();                                    \
    if (fj_set(&fault_ctx[_ft]) == 0) { fault_armed[_ft] = 1; body; } \
    else { mtx_unwind(_msnap); surface_unlock(); clear_clip(); fault_notice(); recover; } \
    fault_ctx[_ft] = _fsav; fault_armed[_ft] = _fasav;                \
} while (0)

int  bmp_load(const u8 *data, u32 n, u8 *out, int outcap, int *w, int *h);
int  text_width(const char *s);
int  font_height(void);
int  text_fit(const char *s, int maxpx);
void cursor_hide(int hide);
void cursor_shape(int shape);

void heap_init(void);
void win_image_trim(void);
void *kmalloc(u32 n);
void *krealloc(void *p, u32 n);
void kfree(void *p);
u32  heap_avail(void);
u32  heap_largest(void);
u32  heap_blocks(void);
u32  heap_base(void);
u32  heap_end(void);
u32  heap_grow_base(void);
u32  heap_grow_end(void);
u32  heap_capacity(void);
u32  heap_limit(void);

extern volatile int kupd_critical;
int  kernel_update(const char *name, char *err, int errcap);
int  kernel_update_data(const u8 *img, u32 size, char *err, int errcap);

extern volatile u32 ticks;

extern u8 timer_alive;
extern char boot_errs[48];
void bmark(char c);
extern volatile u32 irq_counts[16];
void pic_mask(int irq);
void idt_init(void);
void pic_init(void);
void pit_init(void);
void kbd_init(void);
int  kbd_present(void);
int  mouse_init(void);
int  mouse_has_wheel(void);
int  kbd_pop(u8 *sc);
int  kbd_cancel_pending(void);
int  mouse_pop(u32 *pk, u32 *when);
void rtc_read(int *h, int *m, int *s, int *D, int *M, int *Y);
void cpu_init(void);
const char *cpu_brand(void);
u32  cpu_mhz(void);
void cpuid_raw(u32 leaf, u32 out[4]);

void mmx_init(void);
int  mmx_available(void);
extern volatile int in_irq;

int  key_is_down(int key);
void key_clear_held(void);

void mtrr_init(u32 fb_base, u32 fb_len, int banked);
int  mtrr_wc_range(u32 base, u32 len);
int  mtrr_wc_vga_window(void);
int  mtrr_active(void);
void tsc_read(u32 *hi, u32 *lo);
int  irq_register(int irq, void (*fn)(void));
void irq_unregister(int irq);
void speaker_tone(u32 hz);
void speaker_off(void);
u32  pci_cfg_read(u8 bus, u8 dev, u8 fn, u8 off);
extern int plat_emulated;

extern u32 bda_ebda_seg, bda_base_mem_kb;

void threads_init(void);
int  thread_create(void (*fn)(void), const char *name);
void thr_yield(void);
void thr_tick(void);
void thr_preempt_set(int on);
int  thr_preempt_on(void);
void thr_exit(void);
int  threads_selftest(void);
typedef struct { char name[12]; int state; u32 runs, stack_used, stack_size; } ThreadInfo;
int  thread_info(int slot, ThreadInfo *out);
void threads_print(void);
void threads_report(char *out, int cap);
void app_worker(void);
int app_callback(int owner,int win,void *fn,void *ctx,int value,const char *path,int is_path);
int app_owner_busy(int owner);
int app_current_window(void);
int app_job_info(int win,u32 *elapsed,u32 *prog,u32 *io);
int app_unresponsive(int win);
void fault_show_banner(const char *msg);
void app_buffer_lock(void);
void app_buffer_unlock(void);
void app_network_lock(void);
void app_network_unlock(void);
void app_cancel_window(int win);
int app_cancel_pending(void);
void handle_sc(u8 sc);
int pump_keyboard(void);
void keyboard_unwind(void);
void app_forget_window(int win);
void app_local_progress(const char *title,const char *msg,int frac);
int  app_busy(int win);
u32  app_q_dropped(void);
u32  app_q_peak(void);
int  app_q_depth(void);
int  thread_overflowed(void);
void preempt_disable(void);
void preempt_enable(void);
int  preempt_depth(void);
void preempt_restore(int d);
void worker_unwind(int preempt_snap);

typedef struct { int held, owner, depth; } Mutex;
#define MUTEX_INIT { 0, -1, 0 }
void mtx_lock(Mutex *m);
void mtx_unlock(Mutex *m);
int  mtx_held_count(void);
void mtx_unwind(int snap);
void pci_cfg_write(u8 bus, u8 dev, u8 fn, u8 off, u32 v);
int  pci_find(u16 vendor, u16 device, int *bus, int *dev, int *fn);
int  rtc_write(int h, int m, int s, int D, int M, int Y);
int  kbd_mods(void);
u32  rtc_now_dos(void);
void dos_fmt(u32 dt, char *buf);
void reboot(void);

extern volatile u8 fdc_irq_fl;
extern int fdc_ok;
void fdc_init(void);
int  fdc_read(u32 lba, u8 *buf);
int  fdc_write(u32 lba, const u8 *buf);
int  fdc_read_many(u32 lba, u8 *buf, u32 count);
int  fdc_write_many(u32 lba, const u8 *buf, u32 count);
void fdc_tick(void);

int   fs_ensure(void);
FsEnt *fs_slot(int i);
int   fs_read(const char *name, u8 *buf, u32 max);
int   fs_write(const char *name, const u8 *buf, u32 size);
int   fs_delete(const char *name);
int   fs_mkdir(const char *name);
int   fs_is_dir(const char *name);
int   fs_touch(const char *name);
int   fs_rename(const char *oldname, const char *newname);
int   fs_rename_dir(const char *olddir, const char *newdir);
int   fs_dir_count(const char *dir);
int   fs_defrag(void (*prog)(int done, int total));

int   fs_exists(const char *name);
u32   fs_free_kb(void);
const char *ext_type(const char *name);

void usb_init(void);
int  usb_present(void);
extern volatile int usb_quiet;
const char *usb_model(void);
u32  usb_capacity_kb(void);
u32  usb_capacity_sectors(void);
int  usb_read(u32 lba, u32 count, u8 *buf);
int  usb_write(u32 lba, u32 count, const u8 *buf);
void usb_poll(void);
u32  usb_generation(void);

void register_fat(const FatOps *ops);
void register_net(const NetOps *ops);
int  fat_mount(void);
const char *fat_label(void);
int  fat_list(const char *path, FatEnt *out, int max);
int  fat_read(const char *path, u8 *buf, u32 max);
int  fat_writable(void);
int  fat_write(const char *path, const u8 *buf, u32 size);
int  fat_append(const char *path, const u8 *buf, u32 size);
int  fat_delete(const char *path);
int  fat_mkdir(const char *path);
int  fat_rename(const char *path, const char *newname);
int  fat_can_mkdir(void);
int  fat_rmdir(const char *path);
int  fat_exists(const char *path);
u32  fat_total_kb(void);
u32  fat_free_kb(void);
void net_poll(void);
int  net_up(void);
int  net_dhcp(u32 timeout_ticks);
int  net_ping(u32 dst_be, u32 timeout_ticks);
u32  net_get(int what);
void net_set(int what, u32 v);
u32  net_dns(const char *name, u32 timeout_ticks);
u32  net_sntp(u32 ip_be, u32 timeout_ticks);
int  net_http_get(u32 ip_be, u16 port, const char *host, const char *path,
                  int (*sink)(const u8 *chunk, int len, void *ctx),
                  void *ctx, u32 timeout_ticks);
const u8 *net_mac_get(void);
void register_desktop(const DesktopOps *ops);
void desk_draw(void);
void desk_mouse(int x, int y, int ev);
void desk_drop(int x, int y, const char *type, const char *data);
int  desk_key(int k);
int  clip_set(const char *type, const void *data, u32 n);
int  clip_get(const char *type, void *buf, u32 max);
const char *clip_type(void);
void menu_show(int x, int y, const char *const *items, int n,
               void (*pick)(int idx, void *ctx), void *ctx);

void register_dialogs(const DialogOps *ops);
int  clip_set_text(const char *s);
int  clip_get_text(char *buf, int cap);
void msgbox(const char *title, const char *text, int buttons,
            void (*cb)(int result, void *ctx), void *ctx);
void notify(const char *text);
void file_save(const char *title, const char *ext, const char *defname,
               void (*cb)(const char *path, void *ctx), void *ctx);
void file_picker(const char *title, const char *ext, int dirs_only,
                 void (*cb)(const char *path, void *ctx), void *ctx);
void progress_open(const char *title);
void progress_set(int pct, const char *label);
void progress_close(void);

#define IOBUF_SZ (1327104)
extern u8 *const iobuf;
extern char   name_scratch[64][64];
extern FatEnt fe_scratch[128];

#define CFG_MAGIC 0x47464346
#define CFG_LBA   256
#define CFG ((FCfg *)0x7100)
void config_load(void);
int  config_save(void);
int memcmp(const void *a,const void *b,u32 n);
int config_get(const char *key, u32 *value);
int config_set(const char *key, u32 value);
int config_read(void *buf, u32 cap);
int clip_history(int index, ClipInfo *info, void *buf, u32 cap);
int clip_restore(int index);
u32 clip_sequence(void);
void mem_track(const char *name, const void *buf, u32 size);
int mem_buffer(int index, MemBuffer *out);
void mem_untrack(const void *buf);
void fs_cache_clear(void);
void fs_cache_invalidate(u32 lba);
void kext_trim_idle(void);
void app_placeholder(int type,const AppDesc *desc);
void paging_space_drop(int slot);
void services_drop_owner(int owner);
void services_dialog_drop(void);
int services_dialog_owner(void);

#define MAXWIN 12

enum { WT_TERM, WT_SYSINFO, WT_BUILTIN_COUNT };
#define K_MENU 0x200
#define K_PRTSC 0x201
extern Win wins[MAXWIN];
extern int mx, my;
extern u8 gui_dirty;
extern u8 gui_blink;
int  win_is_focused(Win *w);
int  win_is_hovered(Win *w);
int control_state(int x, int y, int w, int h);
void win_redraw(int type, int inst);
void win_close_flush(void);

int  app_handler_running(int win);
int  app_stuck(u32 *elapsed);
void app_note_pump(void);
void app_note_io(void);
extern volatile u32 app_io_tick;
u32  app_progress(void);
void app_kill_request(int win);
void app_kill_poll(void);
void fault_record_hang(const char *who);
extern int hang_stuck_win;
void gui_init(void);
void gui_compose(void);
void gui_invalidate(void);
#include "framegate.inc"
void gui_frame_state(FrameState *s);
void gui_mouse(int dx, int dy, u8 btn, u32 when);
void gui_wheel(int dz);
void gui_key(int k);
void gui_tick(void);
int  win_open(int type);
void win_close(int i);
void win_fit_client(int type, int inst, int cw, int ch);

void set_overlay(void (*draw)(void), int (*mouse)(int x, int y, int ev));
void set_overlay_key(int (*key)(int k));
void overlay_drop_owner(int owner);
int overlay_modal(void);
int  drag_start(const char *type, const char *data);
int  drag_active(void);
void win_set_title(int type, int inst, const char *title);
void win_focus(int type, int inst);
void win_close_self(int type, int inst);
const Win *win_slot(int i);
int  win_max(void);
void anim_claim(int on);
int  mouse_buttons(void);
int  dclick(void);
void drag_rect_begin(void);
int  drag_rect_get(int *x0, int *y0, int *x1, int *y1);
void mouse_warp(int x, int y);
void present(void);
int  present_try(void);
void present_done(void);
int  gui_pump(void);
int  gui_launch_pending(void);

void esc_arm(void);
int  esc_pending(void);

void apps_init(void);
int  register_app(const AppDesc *d);
int  app_count(void);
const AppDesc *app_desc(int type);
int  app_find(const char *title);
int  app_type_owner(int type);
int  app_multi(int type);
int  app_alloc(int type);
void app_free(int type, int inst);
void app_client_size(int type, int inst, int *w, int *h);
void app_min_client(int type, int *w, int *h);
int  app_resizable(int type);
int  app_live_draw(int type);
void app_draw(Win *w, int cx, int cy, int cw, int ch);
void app_mouse(Win *w, int lx, int ly, int ev, int cw, int ch);
void app_drop(Win *w, int lx, int ly, const char *type, const char *data);
void app_key(Win *w, int k);
void app_wheel(Win *w, int dz);
int  apps_animating(void);
void shell_print(const char *s);
u32  used_kb(void);
void dmesg_print(void);
void term_clear(void);
int  term_fx(int mode);
const char *term_hist(int i);
int  shell_win_close(int i);
void register_shell(void (*fn)(const char *line));
int  shell_fallback(const char *line);
int  shell_exec(const char *line);

extern Kapi kapi;
void kext_boot(void);
int app_ensure_loaded(int type);
int kext_lazy_type(void);
extern int boot_shift;

int  kext_load(const char *name);
int  kext_count(void);
const KextInfo *kext_get(int i);
const char *kext_at(u32 eip);
void fault_symbol(u32 address, char *out, int cap);
void fault_snapshot(FaultRec *record);
int  register_cmd(const char *name, const char *usage,
                  void (*fn)(const char *args));
const char *cmd_usage(const char *name);
int  cmd_dispatch(const char *name, const char *args);
int  register_opener(const char *ext,
                     int (*fn)(const char *name, const char *fullpath,
                               const u8 *data, int n));
int  opener_dispatch(const char *name, const char *fullpath,
                     const u8 *data, int n);
int  timer_add(u32 interval, void (*fn)(void *ctx), void *ctx);
void timer_del(int id);
void timers_poll(void);
int kext_timer_busy(int owner);
int  register_key_hook(int (*fn)(int k));
void unregister_key_hook(int (*fn)(int k));
int  key_hook_dispatch(int k);
void register_shutdown(void (*fn)(void));
void shutdown_run(void);
int  register_service(const char *name, const void *ops);
const void *service_get(const char *name);

void boot_print(const char *s);
void boot_fail(const char *code);

int kext_unload(int index);
int thread_kext_busy(int owner);
int services_kext_busy(int owner);
int irq_kext_busy(int owner);
