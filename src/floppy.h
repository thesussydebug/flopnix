#pragma once
#define FLOPPY_ABI 1u
#define FLOPPY_TRACK_SECTORS 18u
typedef struct {
    u32 abi;
    int (*read)(u32 lba,u8 *buf,u32 count);
} FloppyOps;
