#pragma once
#define DEBUG_ABI 3u
enum { DBG_NET=1, DBG_ALLOC=2, DBG_DISK=4, DBG_STACK=8, DBG_FS=16, DBG_POISON=32,
    DBG_DEVICE=64, DBG_REDRAW=128, DBG_HEAP=256, DBG_BACKLOG=512, DBG_SLOW=1024 };
#define DEBUG_HEAP_MAGIC 0x48454150u
typedef struct DebugHeapBlock {
    u32 magic, size;
    struct DebugHeapBlock *next;
    u32 free;
} DebugHeapBlock;
typedef struct {
    u32 abi, flags;
    void (*event)(u32 kind,const char *name,u32 a,u32 b,int result);
    void (*draw)(int win);
    void (*packet)(const u8 *frame,u32 size);
    void (*snapshot)(char *out,u32 cap);
} DebugOps;
typedef struct {
    u32 abi;
    void (*bind)(const DebugOps *ops);
    u32 (*guards)(void);
    int (*append)(const char *path,const u8 *buf,u32 size);
    void (*place)(int type);
    int (*queue_depth)(void);
    u32 (*queue_dropped)(void);
    u32 (*queue_peak)(void);
    int (*job_info)(int win,u32 *elapsed,u32 *progress,u32 *io);
} DebugCore;
