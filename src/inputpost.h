#pragma once

#define INPUT_ABI 1u
typedef struct {
    u32 abi;
    int (*post)(int mouse, u32 value);
} InputOps;

/* Real PS/2 packets always set bit 3, so a clear bit 3 marks an absolute remote pointer. */
static inline u32 input_pointer(int x, int y, int buttons, int wheel)
{
    return (u32)(buttons & 7) | ((u32)(x & 1023) << 4) | ((u32)(y & 1023) << 14) |
           ((u32)(wheel & 255) << 24);
}

static inline int input_synthetic(u32 pk, int *x, int *y)
{
    if (pk & 8) return 0;
    *x = (int)((pk >> 4) & 1023);
    *y = (int)((pk >> 14) & 1023);
    return 1;
}
