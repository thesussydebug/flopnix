/* Ctrl+Break handler thing */
#pragma once
#define BRK_FROZEN 200u
#define BRK_EXPIRE 50u
#define BRK_BEEP_HZ 880
#define BRK_BEEP_TICKS 15
enum { BRK_NONE, BRK_PAUSE, BRK_HARD };
enum { BRK_WAIT, BRK_SOFT, BRK_REFUSE, BRK_DROP, BRK_EMERGENCY };

typedef struct { u32 stall, since; u8 req, main, armed, locks, usb, pumping; } BrkIn;

static inline int brk_scan(u32 *st, u8 b)
{
    u32 h = *st = *st << 8 | b;
    if ((h & 0xFFFFFF) == 0xE11D45) return BRK_PAUSE;
    if ((h & 0xFFFF) == 0xE046) return BRK_HARD;
    return BRK_NONE;
}

static inline int brk_accept(int key, u32 stall, int update)
{
    return stall >= BRK_FROZEN && !update ? key : BRK_NONE;
}

static inline int brk_decide(const BrkIn *b)
{
    if (!b->req || b->stall < b->since) return BRK_DROP;
    if (!b->main && b->stall - b->since < BRK_EXPIRE) return BRK_WAIT;
    if (b->req == BRK_HARD) return BRK_EMERGENCY;
    if (!b->main || !b->armed || b->locks || b->usb || b->pumping) return BRK_REFUSE;
    return BRK_SOFT;
}
