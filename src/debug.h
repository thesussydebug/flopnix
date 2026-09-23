#pragma once
#define DEBUG_ABI 2u
enum { DBG_NET=1, DBG_ALLOC=2, DBG_DISK=4, DBG_STACK=8, DBG_FS=16, DBG_POISON=32 };
typedef struct {
    u32 abi, flags;
    void (*event)(u32 kind,const char *name,u32 a,u32 b,int result);
    void (*draw)(void);
    void (*packet)(const u8 *frame,u32 size);
    void (*snapshot)(char *out,u32 cap);
} DebugOps;
typedef struct {
    u32 abi;
    void (*bind)(const DebugOps *ops);
    u32 (*guards)(void);
    int (*append)(const char *path,const u8 *buf,u32 size);
    void (*place)(int type);
} DebugCore;
