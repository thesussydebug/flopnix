/* The function table that connects extensions to the kernel. */
#pragma once

#ifndef FLOPNIX_TYPES
#define FLOPNIX_TYPES
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed   char  i8;
typedef signed   short i16;
typedef signed   int   i32;
#endif

/* New fields are appended to keep existing extension offsets stable. */
#define KAPI_VERSION 36

#define KEXT_MAGIC 0x5458454B
enum { KEXT_KIND_KERNEL = 1, KEXT_KIND_APP = 2 };
#define KEXT_RECLAIMABLE 1

typedef struct { char name[24]; u32 base, size; int owner; } MemBuffer;
typedef struct { char type[12]; u32 size, sequence; } ClipInfo;
typedef struct {
    u32 magic;
    u16 api_version;
    u8  kind;
    u8  pad;
    char name[16];
} KextHeader;

enum {
    C_BLACK = 0, C_NAVY, C_GREEN, C_TEAL, C_MAROON, C_PURPLE, C_OLIVE, C_SILVER,
    C_GRAY, C_BBLUE, C_BGREEN, C_CYAN, C_RED, C_MAGENTA, C_YELLOW, C_WHITE,
    C_G0 = 16,
    C_TB0 = 24,
    C_DESK = 32,
    C_FACE, C_LIGHT, C_SHAD, C_DARK,
    C_TERMBG, C_TERMFG, C_HILITE
};

enum {
    K_UP = 0x100, K_DOWN, K_LEFT, K_RIGHT,
    K_HOME, K_END, K_DEL, K_PGUP, K_PGDN
};

enum { EV_PRESS, EV_DRAG, EV_RELEASE, EV_RPRESS };

#define MAXINST 3
#define SB_W 13

typedef struct {
    u8  used, type, inst;
    int x, y, w, h;
    const char *title;

    char tbuf[24];
    u8   tbuf_on;
} Win;

enum {
    BI_SCREEN_W, BI_SCREEN_H, BI_PITCH, BI_BPP,
    BI_VBE_MODE, BI_LFB_ADDR, BI_MEM_KB, BI_BOOT_DIAG, BI_BOOT_STAGE, BI_VBE,

    BI_EBDA_SEG, BI_BASE_MEM_KB
};

enum {
    DS_OPS, DS_RETRIED, DS_FAILED,
    DS_LAST_MS, DS_WORST_MS, DS_LAST_TRIES, DS_LAST_LBA,
    DS_ST0, DS_ST1, DS_ST2
};

enum {
    MI_TOTAL_KB,
    MI_KERNEL_BASE, MI_KERNEL_END,
    MI_DMA_BASE,    MI_DMA_END,
    MI_FB_BASE,     MI_FB_END,
    MI_STACK_TOP,
    MI_ARENA_BASE,  MI_ARENA_END,  MI_ARENA_RO, MI_ARENA_RW,
    MI_POOL_BASE,   MI_POOL_END,   MI_POOL_USED,
    MI_HEAP_BASE,   MI_HEAP_END,   MI_HEAP_FREE, MI_HEAP_LARGEST,
    MI_HEAP_BLOCKS,
    MI_PAGING,      MI_PAGES,
    MI_KERNEL_BYTES, MI_IO_BASE, MI_IO_END,
    MI_HEAP_GROW_BASE, MI_HEAP_GROW_END, MI_HEAP_CAPACITY, MI_HEAP_LIMIT
};

#define FS_NAMELEN 24
#define FS_MAXFILE 524288

#define FS_NFILES 128
typedef struct {
    char name[FS_NAMELEN];
    u32  size;
    u32  mtime;
    u16  start, nsect;
    u8   used, attr;
    u8   pad[2];
} FsEnt;

#define FS_EIO (-2)

#define FS_ATTR_DIR 0x10

typedef struct {
    char name[64];
    u32  size;
    u32  mtime;
    u8   is_dir;
} FatEnt;

typedef struct {
    u32 magic;
    u8  video;
    u8  net_mode;
    u8  mouse_speed;
    u8  ss_enable;
    u32 ip, mask, gw;
    u8  ss_secs;

    u8  wp_mode;
    u8  wp_unused;
    char wp_path[64];

    u8  wp_col[3];
    u8  wp_ga[3], wp_gb[3];

    i8  tz_qh;
    u8  reserved[414];
} FCfg;

enum { APP_LIVE_DRAW=1, APP_INDEPENDENT=2 };

enum { APP_CAT_AUTO, APP_CAT_PROGRAMS, APP_CAT_GAMES, APP_CAT_SYSTEM,
       APP_CAT_DEV };

typedef struct {
    const char *title;
    u8   max_inst;
    u8   resizable;
    u8   in_menu;
    void (*open)(int inst);
    void (*draw)(Win *w, int cx, int cy, int cw, int ch);
    void (*key)(int inst, int k);
    void (*mouse)(int inst, int lx, int ly, int ev, int cw, int ch);
    void (*wheel)(int inst, int dz);
    void (*client_size)(int inst, int *w, int *h);
    void (*min_client)(int *w, int *h);

    void (*drop)(int inst, int lx, int ly, const char *type, const char *data);

    u8 category;

    void (*close)(int inst);

    u8 live_draw;
} AppDesc;

typedef struct {
    void (*draw)(void);
    void (*mouse)(int x, int y, int ev);
    void (*drop)(int x, int y, const char *type, const char *data);

    int  (*clip_set)(const char *type, const void *data, u32 n);
    int  (*clip_get)(const char *type, void *buf, u32 max);
    const char *(*clip_type)(void);

    void (*menu_show)(int x, int y, const char *const *items, int n,
                      void (*pick)(int idx, void *ctx), void *ctx);

    int  (*key)(int k);
} DesktopOps;

typedef struct {
    u32  vec;
    u32  err;
    u32  eip;
    u32  tick;
    char owner[16];
} FaultRec;

typedef struct {
    char name[FS_NAMELEN];
    char hname[16];
    u32  base, size;
    u8   kind;
    int  status;
} KextInfo;

enum { MB_OK = 1, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL };

#define FAULT_VEC_HANG 0xFE
enum { MBR_OK, MBR_CANCEL, MBR_YES, MBR_NO };
enum { CUR_ARROW, CUR_BUSY, CUR_TEXT, CUR_HAND, CUR_CROSS };

typedef struct {
    u8  *buf;
    u32  cap;
    u32  len;
    u32  pos;
    u8   owned;
} MemStream;

typedef struct {
    void (*msgbox)(const char *title, const char *text, int buttons,
                   void (*cb)(int result, void *ctx), void *ctx);
    void (*notify)(const char *text);
    void (*file_picker)(const char *title, const char *ext, int dirs_only,
                        void (*cb)(const char *path, void *ctx), void *ctx);
    void (*progress_open)(const char *title);
    void (*progress_set)(int pct, const char *label);
    void (*progress_close)(void);

    void (*file_save)(const char *title, const char *ext, const char *defname,
                      void (*cb)(const char *path, void *ctx), void *ctx);
} DialogOps;

/* Identifies the supported tail of the FAT service table. */
#define FAT_ABI 0x46415434u
typedef struct {
    int  (*mount)(void);
    int  (*list)(const char *path, FatEnt *out, int max);
    int  (*read)(const char *path, u8 *buf, u32 max);
    int  (*write)(const char *path, const u8 *buf, u32 size);
    int  (*del)(const char *path);
    int  (*writable)(void);
    const char *(*label)(void);
    u32  (*total_kb)(void);
    u32  (*free_kb)(void);

    u32  abi;
    int  (*mkdir)(const char *path);
    int  (*rename)(const char *path, const char *newname);
    int  (*rmdir)(const char *path);

    int  (*append)(const char *path, const u8 *buf, u32 size);

    int  (*exists)(const char *path);
} FatOps;

enum { NET_IP, NET_MASK, NET_GW, NET_DHCP_OK, NET_DNS, NET_LINK,
       NET_ADAPTER, NET_IO, NET_STATE, NET_RX, NET_TX, NET_LEASE_LEFT,
       NET_DNS2, NET_MTU, NET_RENEW, NET_CONFIG };

#define NET_LINK_PHY   0x0001u
#define NET_LINK_UP    0x0002u
#define NET_LINK_FULL  0x0004u
#define NET_LINK_MBPS(v) (((v) >> 8) & 0xFFu)
#define NET_ABI_V11 0x4E455431u
#define NET_ABI_V12 0x4E455432u
typedef struct {
    void (*poll)(void);
    int  (*up)(void);
    int  (*dhcp)(u32 timeout_ticks);
    int  (*ping)(u32 dst_be, u32 timeout_ticks);
    u32  (*get)(int what);
    void (*set)(int what, u32 v);
    const u8 *(*mac)(void);

    u32 v11;
    u32 (*dns)(const char *name, u32 timeout_ticks);
    int (*http_get)(u32 ip_be, u16 port, const char *host, const char *path,
                    int (*sink)(const u8 *chunk, int len, void *ctx),
                    void *ctx, u32 timeout_ticks);

    u32 v12;
    u32 (*sntp)(u32 ip_be, u32 timeout_ticks);
} NetOps;

typedef struct {
    u32 version;  /* Reports the running kernel's API version. */
    const char *os_version;  /* Returns the OS version string. */

    void (*fill_rect)(int x, int y, int w, int h, u8 c);
    void (*hline)(int x, int y, int w, u8 c);
    void (*vline)(int x, int y, int h, u8 c);
    void (*bevel)(int x, int y, int w, int h, int sunken);
    void (*panel)(int x, int y, int w, int h, int sunken);
    void (*draw_char)(int x, int y, char ch, u8 fg);
    void (*draw_text)(int x, int y, const char *s, u8 fg);
    void (*draw_text_clip)(int x, int y, const char *s, u8 fg, int maxpx);
    void (*draw_text_clip2)(int x, int y, const char *s, u8 fg, int cx0, int cx1);
    void (*draw_sbar)(int x, int y, int len, int horiz, int total, int vis, int off);
    int  (*sbar_from_pos)(int len, int total, int vis, int pos);
    void (*focus_rect)(int x, int y, int w, int h);
    void (*blit)(int x, int y, int w, int h, const u8 *src, int spitch);
    void (*palette_rgb)(int idx, u8 *r, u8 *g, u8 *b);
    u8   (*palette_nearest)(u8 r, u8 g, u8 b);
    const int *screen_w, *screen_h;

    int  (*register_app)(const AppDesc *d);  /* Registers an app; returns its ID, or -1 on failure. */
    int  (*app_find)(const char *title);  /* Finds an app ID by title, or returns -1. */
    int  (*win_open)(int type);  /* Opens or raises a window; returns its instance, or -1. */
    void (*win_fit_client)(int type, int inst, int cw, int ch);
    int  (*win_is_focused)(Win *w);
    const int *mouse_x, *mouse_y;  /* Points to the live mouse coordinates. */
    const u8  *gui_blink;  /* Returns the caret blink phase, 0 or 1. */

    int  (*register_cmd)(const char *name, const char *usage,
                         void (*fn)(const char *args));
    void (*shell_print)(const char *s);  /* Writes text from a shell command handler. */

    int  (*register_opener)(const char *ext,
                            int (*fn)(const char *name, const char *fullpath,
                                      const u8 *data, int n));  /* Registers a file handler; an empty extension matches other files. */
    int  (*open_with)(const char *name, const char *fullpath,
                      const u8 *data, int n);  /* Tries the registered handlers for a file. */

    int  (*fs_read)(const char *name, u8 *buf, u32 max);
    int  (*fs_write)(const char *name, const u8 *buf, u32 size);  /* Writes a file; -1 is error, -2 is full, and -3 is a folder. */
    int  (*fs_delete)(const char *name);
    int  (*fs_exists)(const char *name);
    u32  (*fs_free_kb)(void);
    FsEnt *(*fs_slot)(int i);  /* Reads a file-table slot, which may be unused. */
    const char *(*ext_type)(const char *name);  /* Returns a readable file type, such as Text or Image. */

    int  (*fat_mount)(void);  /* Mounts the USB FAT volume if its service is loaded. */
    int  (*fat_list)(const char *path, FatEnt *out, int max);
    int  (*fat_read)(const char *path, u8 *buf, u32 max);
    int  (*fat_write)(const char *path, const u8 *buf, u32 size);  /* Writes a USB file; -1 is error, -2 is full, and -3 is a folder. */
    int  (*fat_delete)(const char *path);
    int  (*fat_writable)(void);
    const char *(*fat_label)(void);
    u32  (*fat_total_kb)(void);
    u32  (*fat_free_kb)(void);

    int  (*net_up)(void);
    int  (*net_dhcp)(u32 timeout_ticks);
    int  (*net_ping)(u32 dst_be, u32 timeout_ticks);
    int  (*net_parse_ip)(const char *s, u32 *out);  /* Parses an IPv4 address without needing a network service. */
    u32  (*net_get)(int what);  /* Reads the value of a NET_* setting. */
    void (*net_set)(int what, u32 v);
    const u8 *(*net_mac)(void);

    int  (*usb_present)(void);
    int  (*usb_read)(u32 lba, u32 count, u8 *buf);
    int  (*usb_write)(u32 lba, u32 count, const u8 *buf);
    u32  (*usb_capacity_kb)(void);
    u32  (*usb_capacity_sectors)(void);
    const char *(*usb_model)(void);

    void (*register_fat)(const FatOps *ops);
    void (*register_net)(const NetOps *ops);

    void (*outb)(u16 port, u8 v);
    u8   (*inb)(u16 port);
    void (*outw)(u16 port, u16 v);
    u16  (*inw)(u16 port);
    void (*outl)(u16 port, u32 v);
    u32  (*inl)(u16 port);

    FCfg *cfg;
    int  (*config_save)(void);  /* Saves settings; returns 1 on success. */
    void (*reboot)(void);

    const volatile u32 *ticks;  /* Points to the 100 Hz tick count when the timer works. */
    const u8 *timer_alive;
    void (*rtc_read)(int *h, int *m, int *s, int *D, int *M, int *Y);
    u32  (*rtc_now_dos)(void);
    void (*dos_fmt)(u32 dt, char *buf);

    void (*kfmt)(char *dst, int cap, const char *f, ...);
    u32  (*strlen)(const char *s);
    int  (*strcmp)(const char *a, const char *b);
    int  (*strncmp)(const char *a, const char *b, u32 n);
    int  (*strcasecmp)(const char *a, const char *b);
    void (*strlcpy)(char *d, const char *s, int cap);
    void *(*memcpy)(void *d, const void *s, u32 n);
    void *(*memmove)(void *d, const void *s, u32 n);
    void *(*memset)(void *d, int c, u32 n);
    void (*human_size)(u32 bytes, char *buf, int cap);
    void (*human_size_kb)(u32 kb, char *buf, int cap);

    u8  *iobuf;
    u32  iobuf_size;

    void (*register_desktop)(const DesktopOps *ops);  /* Registers desktop callbacks from a kernel extension. */

    int  (*clip_set)(const char *type, const void *data, u32 n);  /* Copies a typed payload into the clipboard. */
    int  (*clip_get)(const char *type, void *buf, u32 max);  /* Reads clipboard bytes; returns their count, or -1. */
    const char *(*clip_type)(void);
    void (*menu_show)(int x, int y, const char *const *items, int n,
                      void (*pick)(int idx, void *ctx), void *ctx);

    int  (*drag_start)(const char *type, const char *data);  /* Starts a drag; file payloads use a:NAME or u:/PATH. */
    int  (*drag_active)(void);

    void (*set_overlay)(void (*draw)(void), int (*mouse)(int x, int y, int ev));  /* Installs one overlay; its mouse handler returns 0 to close it. */

    void (*gui_dirty)(void);

    const char *(*cpu_brand)(void);
    u32  (*cpu_mhz)(void);
    u32  (*mem_total_kb)(void);
    int  (*kext_count)(void);
    const KextInfo *(*kext_get)(int i);

    void (*pixel)(int x, int y, u8 c);
    u8   (*getpixel)(int x, int y);  /* Reads a pixel from the backbuffer. */
    void (*line)(int x0, int y0, int x1, int y1, u8 c);
    void (*rect)(int x, int y, int w, int h, u8 c);  /* Draws a rectangle outline. */
    void (*circle)(int cx, int cy, int r, u8 c);
    void (*fill_circle)(int cx, int cy, int r, u8 c);
    void (*blit_key)(int x, int y, int w, int h, const u8 *src, int spitch,
                     u8 key);  /* Draws a bitmap while skipping its transparent color. */
    void (*read_rect)(int x, int y, int w, int h, u8 *dst, int dpitch);
    void (*draw_text_scaled)(int x, int y, const char *s, u8 fg, int sx, int sy);

    void (*set_clip)(int x, int y, int w, int h);  /* Narrows drawing bounds; restore the app's bounds after use. */
    void (*clear_clip)(void);  /* Resets clipping to the whole screen. */

    void (*palette_set)(int idx, u8 r, u8 g, u8 b);  /* Changes a palette entry; colors 0 through 47 belong to the system. */
    const u8 *(*font_glyph)(char ch);  /* Returns 16 row bytes for an 8-by-16 character bitmap. */

    void (*win_set_title)(int type, int inst, const char *title);
    void (*win_close_self)(int type, int inst);
    void (*win_focus)(int type, int inst);
    const Win *(*win_slot)(int i);  /* Returns a window slot, or null when unused. */
    int  (*win_max)(void);

    void (*anim_claim)(int on);  /* Adds or removes an animation claim; pair each on with an off. */
    int  (*mouse_buttons)(void);  /* Reads held buttons: bit 0 is left, bit 1 is right. */
    int  (*kbd_mods)(void);  /* Reads modifier bits: Shift 1, Ctrl 2, Caps Lock 4, Alt 8. */

    void *(*kmalloc)(u32 n);  /* Allocates memory aligned to 8 bytes, or returns null. */
    void (*kfree)(void *p);
    u32  (*heap_avail)(void);  /* Returns the number of free heap bytes. */

    int  (*timer_add)(u32 interval, void (*fn)(void *ctx), void *ctx);  /* Runs a callback in the main loop; returns its ID, or -1. */
    void (*timer_del)(int id);

    int  (*register_key_hook)(int (*fn)(int k));  /* Registers a global key handler. */
    void (*unregister_key_hook)(int (*fn)(int k));
    void (*register_shutdown)(void (*fn)(void));  /* Registers a callback to run before reboot. */

    int  (*register_service)(const char *name, const void *ops);
    const void *(*service_get)(const char *name);  /* Finds a loaded service, or returns null. */

    u32  (*boot_info)(int what);  /* Reads a BI_* boot information value. */
    void (*cpuid_raw)(u32 leaf, u32 out[4]);
    void (*tsc_read)(u32 *hi, u32 *lo);  /* Reads the CPU timestamp counter. */
    u32  (*pci_cfg_read)(u8 bus, u8 dev, u8 fn, u8 off);
    void (*pci_cfg_write)(u8 bus, u8 dev, u8 fn, u8 off, u32 v);
    int  (*pci_find)(u16 vendor, u16 device, int *bus, int *dev, int *fn);

    int  (*irq_register)(int irq, void (*fn)(void));  /* Adds an IRQ handler; the kernel acknowledges the interrupt. */
    void (*irq_unregister)(int irq);
    void (*insw_rep)(u16 port, void *buf, u32 nwords);  /* Reads repeated 16-bit words from an I/O port. */
    void (*outsw_rep)(u16 port, const void *buf, u32 nwords);

    void (*speaker_tone)(u32 hz);  /* Starts a sustained PC speaker tone. */
    void (*speaker_off)(void);

    void (*sleep_ms)(u32 ms);  /* Waits for timer ticks with a bounded fallback. */

    int  (*rtc_write)(int h, int m, int s, int D, int M, int Y);  /* Sets the hardware clock. */

    int  (*atoi)(const char *s);
    const char *(*strstr)(const char *hay, const char *needle);
    const char *(*strchr)(const char *s, int c);
    int  (*toupper)(int c);
    int  (*tolower)(int c);
    void (*ksort)(void *base, int n, int size,
                  int (*cmp)(const void *, const void *));
    u32  (*rand)(void);  /* Returns the next pseudorandom value. */
    void (*rand_seed)(u32 seed);

    void (*klog)(const char *s);  /* Appends a message to the kernel log. */
    int  (*klog_read)(char *dst, int cap);  /* Copies log bytes and returns the number copied. */

    int  (*shell_exec)(const char *line);  /* Runs a shell command. */

    int  (*kernel_update)(const char *name, char *err, int errcap);
    int  (*kernel_update_data)(const u8 *img, u32 size, char *err, int errcap);

    u32  (*usb_gen)(void);

    void (*msgbox)(const char *title, const char *text, int buttons,
                   void (*cb)(int result, void *ctx), void *ctx);
    void (*notify)(const char *text);
    void (*file_picker)(const char *title, const char *ext, int dirs_only,
                        void (*cb)(const char *path, void *ctx), void *ctx);
    void (*progress_open)(const char *title);
    void (*progress_set)(int pct, const char *label);
    void (*progress_close)(void);
    void (*register_dialogs)(const DialogOps *ops);  /* Registers dialog callbacks from a kernel extension. */

    int  (*clip_set_text)(const char *s);
    int  (*clip_get_text)(char *buf, int cap);  /* Copies clipboard text; returns its length, or -1. */

    void (*broadcast)(const char *event, const char *data);
    int  (*on_event)(void (*fn)(const char *event, const char *data));
    void (*off_event)(void (*fn)(const char *event, const char *data));

    int  (*bmp_load)(const u8 *data, u32 n, u8 *out, int outcap, int *w, int *h);

    int  (*text_width)(const char *s);  /* Returns the width of text in pixels. */
    int  (*font_height)(void);  /* Returns the character height in pixels. */
    int  (*text_fit)(const char *s, int maxpx);  /* Counts the characters that fit in a pixel width. */

    void (*cursor_hide)(int hide);  /* Hides the pointer with 1 and shows it with 0. */
    void (*cursor_shape)(int shape);  /* Selects a CUR_* pointer shape. */
    void (*mouse_warp)(int x, int y);  /* Moves the mouse pointer. */

    void (*present)(void);

    int  (*dclick)(void);

    void (*drag_rect_begin)(void);
    int  (*drag_rect_get)(int *x0, int *y0, int *x1, int *y1);

    void (*uuid_gen)(char *out);

    const char *(*path_base)(const char *p);  /* Returns the file-name part of a path. */
    const char *(*path_ext)(const char *p);  /* Returns the extension without its dot, or an empty string. */
    void (*path_dir)(const char *p, char *out, int cap);  /* Copies the directory part of a path. */
    void (*path_join)(char *out, int cap, const char *dir, const char *name);

    void (*date_fmt)(u32 dt, const char *fmt, char *out, int cap);

    int  (*b64_encode)(const u8 *in, u32 n, char *out, int cap);  /* Encodes Base64 and returns the character count. */
    int  (*b64_decode)(const char *in, u8 *out, int cap);  /* Decodes Base64 and returns the byte count. */

    u32  (*crc32)(const void *data, u32 n);
    u32  (*hash_fnv)(const void *data, u32 n);  /* Calculates a 32-bit FNV-1a hash. */

    void (*ms_open)(MemStream *s, u8 *buf, u32 cap);  /* Opens a fixed buffer as a memory stream. */
    int  (*ms_alloc)(MemStream *s, u32 cap);  /* Creates a growing memory stream; returns 1 on success. */
    void (*ms_free)(MemStream *s);
    int  (*ms_write)(MemStream *s, const void *data, u32 n);  /* Writes to a memory stream and returns the byte count. */
    int  (*ms_read)(MemStream *s, void *out, u32 n);
    int  (*ms_seek)(MemStream *s, u32 pos);

    int  (*shell_history_count)(void);
    const char *(*shell_history)(int i);  /* Reads a history entry, ordered from oldest to newest. */

    int  (*surface_lock)(u8 **px, int *pitch, int *w, int *h);  /* Grants direct framebuffer access; returns 1 on success. */
    void (*surface_unlock)(void);
    void (*clip_rect_get)(int *x, int *y, int *w, int *h);  /* Direct pixel writes must stay inside these bounds. */

    void (*set_overlay_key)(int (*key)(int k));  /* Sets the current overlay's key handler; 1 consumes a key. */
    void (*file_save)(const char *title, const char *ext, const char *defname,
                      void (*cb)(const char *path, void *ctx), void *ctx);

    u32  (*net_dns)(const char *name, u32 timeout_ticks);  /* Resolves a host to a network-order IPv4 address, or returns 0. */
    int  (*net_http_get)(u32 ip_be, u16 port, const char *host,
                         const char *path,
                         int (*sink)(const u8 *chunk, int len, void *ctx),
                         void *ctx, u32 timeout_ticks);

    int  (*gui_pump)(void);  /* Updates the clock, animation, and pointer; returns 1 for Escape. */

    void (*term_clear)(void);  /* Clears the active terminal. */
    int  (*term_fx)(int mode);  /* Sets matrix mode with 1 or rainbow mode with 2; returns the state. */
    const char *(*term_hist)(int i);  /* Returns a command history line, or null. */
    int  (*win_close)(int idx);  /* Closes a window by ID. */

    int  (*disk_read)(u32 lba, u8 *buf);  /* Reads one raw 512-byte floppy sector. */

    int  (*fs_defrag)(void (*prog)(int done, int total));  /* Compacts files; negative results report failure or damaged data. */
    void (*register_shell)(void (*fn)(const char *line));  /* Registers a fallback shell command handler. */
    u32  (*mem_used_kb)(void);  /* Counts kernel and app allocations and fixed buffers. */
    const char *(*cmd_usage)(const char *name);  /* Returns usage text for a registered command. */
    void (*dmesg)(void);  /* Prints the boot diagnostics log. */
    u32  (*cpu_usage)(int app_type);  /* Reports app CPU use in thousandths; a negative ID selects system and idle. */
    int  (*app_count)(void);  /* Returns the number of registered app types. */
    const AppDesc *(*app_desc)(int type);
    int  (*kext_load)(const char *name);  /* Loads an extension at runtime. */

    int  (*key_down)(int key);  /* Tests whether a key is held; focus changes clear held keys. */

    int  (*fault_count)(void);
    const FaultRec *(*fault_get)(int i);

    int  (*fs_mkdir)(const char *name);
    int  (*fs_is_dir)(const char *name);
    int  (*fs_rename)(const char *oldname, const char *newname);
    int  (*fs_rename_dir)(const char *olddir, const char *newdir);
    int  (*fs_dir_count)(const char *dir);

    int  (*fat_mkdir)(const char *path);
    int  (*fat_rename)(const char *path, const char *newname);
    int  (*fat_can_mkdir)(void);
    int  (*fat_rmdir)(const char *path);  /* Removes an empty USB folder; -3 means it is not empty. */

    void (*busy_set)(const char *title, const char *msg, int frac256);
    void (*busy_end)(void);

    const char *os_build_date;

    int (*mem_mapped)(u32 va);  /* Checks whether an address range is mapped before reading it. */
    void (*threads)(void);  /* Returns thread slots and stack usage. */

    void (*ktrace)(const char *s);

    u32 (*disk_stat)(int what);

    const char *(*shell_cwd)(void);
    int  (*shell_set_cwd)(const char *dir);

    u32 (*mem_info)(int what);

    int (*ring3_ready)(void);
    u32 (*ring3_test)(int what);

    int (*fs_touch)(const char *name);

    int (*fat_exists)(const char *path);

    void (*esc_arm)(void);
    int  (*esc_pending)(void);

    int  (*mem_writable)(u32 va);

    int  (*mem_poke)(u32 va, u8 val);

    int  (*win_is_hovered)(Win *w);

    u32  (*net_sntp)(u32 ip_be, u32 timeout_ticks);

    int (*kext_unload)(int index);
    int (*config_get)(const char *key, u32 *value);
    int (*config_set)(const char *key, u32 value);
    int (*config_read)(void *buf, u32 cap);
    int (*clip_history)(int index, ClipInfo *info, void *buf, u32 cap);
    int (*clip_restore)(int index);
    u32 (*clip_sequence)(void);
    void (*mem_track)(const char *name, const void *buf, u32 size);
    int (*mem_buffer)(int index, MemBuffer *out);
    int (*control_state)(int x, int y, int w, int h);
    void (*win_redraw)(int type, int inst);
    void (*buffer_lock)(void);
    void (*buffer_unlock)(void);
    void (*network_lock)(void);
    void (*network_unlock)(void);
    void *(*krealloc)(void *ptr, u32 size);
} Kapi;
