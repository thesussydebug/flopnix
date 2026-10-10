#pragma once
#define DISK_TOOLS_ABI 1u
#define DT_ON 1
#define DT_HOME 2
#define DT_COPY 4
typedef struct {
    u32 abi;
    void (*sysmem)(const char *args, void (*print)(const char *text));
    void (*diskcopy)(const char *args, void (*print)(const char *text));
    int (*state)(int *files, u32 *bytes, int *waiting);
} DiskToolsOps;
