#pragma once
#define MANAGER_ABI 1u

typedef struct {
    u32 abi;
    int (*close_request)(int index);
} ManagerOps;
