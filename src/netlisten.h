#pragma once

#define NET_LISTEN_ABI 1u
typedef struct {
    u32 abi;
    int (*listen)(u16 port);
    void (*stop)(void);
    u32 (*session)(void);
    int (*read)(u32 session, u8 *data, int capacity);
    int (*write)(u32 session, const u8 *data, int length);
    void (*close)(u32 session);
    void (*poll)(void);
    int (*pending)(u32 session);
} NetListenOps;
