#pragma once

#define NET_STREAM_ABI 1u
typedef struct {
    u32 abi;
    int (*open)(u32 ip, u16 port, u32 timeout);
    int (*read)(u8 *data, int capacity);
    int (*write)(const u8 *data, int length);
    void (*close)(void);
    void (*poll)(void);
} NetStreamOps;
