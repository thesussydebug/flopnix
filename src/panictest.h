#pragma once

#define PANIC_TEST_ABI 1u
typedef struct {
    u32 abi;
    void (*stop)(int emergency);
} PanicTestOps;
