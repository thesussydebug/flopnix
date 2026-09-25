#pragma once
#define NET_DEBUG_ABI 1u

typedef struct {
    u32 abi;
    void (*set_auto)(int enabled);
    int (*get_auto)(void);
} NetDebugOps;
