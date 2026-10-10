#pragma once
#define FLOPPY_ABI 1u
#define FLOPPY_ABI_WRITE 2u
#define FLOPPY_TRACK_SECTORS 18u
typedef struct {
    u32 abi;
    int (*read)(u32 lba,u8 *buf,u32 count);
    int (*write)(u32 lba,const u8 *buf,u32 count);
    int (*verify)(u32 lba,const u8 *buf,u32 count);
} FloppyOps;
