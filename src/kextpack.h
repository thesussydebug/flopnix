#pragma once
#define KEXT_PACK_ABI 2u
#define KP_HEAD  24u
#define KP_SLACK 32u

typedef struct {
    u32 abi;
    int (*load)(const char *name, u8 **image);
    int (*open)(const u8 *data, u32 length, u8 **image);
    int (*kind)(const char *name);
    int (*gunzip)(const u8 *data, u32 length, u8 **out);
} KextPackOps;

static inline u32 kp_size(const u8 *h, u32 n)
{
    if (n < KP_HEAD || h[0] != 0x1F || h[1] != 0x8B || h[2] != 8 || h[3] != 4 ||
        h[10] != 12 || h[11] || h[12] != 'F' || h[13] != 'X' || h[14] != 8 || h[15] || h[20] > 2) return 0;
    return (u32)h[16] | (u32)h[17] << 8 | (u32)h[18] << 16 | (u32)h[19] << 24;
}
