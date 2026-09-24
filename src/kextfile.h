#pragma once

#define KEXT_FILE_ABI 1u
typedef struct {
    u32 abi;
    int (*check)(const u8 *data, u32 length, KextHeader *header);
    int (*replace)(const char *path, const u8 *data, u32 length);
} KextFileOps;
