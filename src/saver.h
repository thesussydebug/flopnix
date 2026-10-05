#pragma once

#define SAVER_ABI 1u
typedef struct {
    u32 abi;
    void (*draw)(void);
} SaverOps;
