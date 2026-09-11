/* Builds Tulip adapter packet descriptors. */
#pragma once

#define TD_OWN 0x80000000u

static inline int td_rx_ok(u32 status)
{
    if (status & TD_OWN) return 0;
    if (status & 0x8000) return 0;
    if ((status & 0x0300) != 0x0300) return 0;
    return ((status >> 16) & 0x3FFF) > 4;
}

static inline int td_rx_len(u32 status)
{
    return (int)((status >> 16) & 0x3FFF) - 4;
}

static inline u32 td_tx_len(int len, int last_slot)
{
    return 0x60000000u | (last_slot ? 0x02000000u : 0) | (u32)len;
}

static inline u32 td_rx_buf(int size, int last_slot)
{
    return (last_slot ? 0x02000000u : 0) | (u32)size;
}
