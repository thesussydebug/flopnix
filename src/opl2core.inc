/* Calculates OPL2 note frequencies and register values. */
#pragma once

#define OPL_CHANS   9
#define OPL_PORT    0x388

static const unsigned short opl_fnum_tab[12] = {
    345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 614, 651
};

static inline int opl_block(int note)
{
    int b = note / 12 - 1;
    if (b < 0) b = 0;
    if (b > 7) b = 7;
    return b;
}

static inline int opl_fnum(int note)
{
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return opl_fnum_tab[note % 12];
}

static inline int opl_op1(int ch)
{
    return (ch / 3) * 8 + (ch % 3);
}

static inline int opl_op2(int ch)
{
    return opl_op1(ch) + 3;
}

static inline int opl_tl(int vel)
{
    if (vel < 0) vel = 0;
    if (vel > 127) vel = 127;
    return (127 - vel) * 32 / 127;
}

static inline int opl_pitch(int note, int on, int *lo)
{
    int f = opl_fnum(note), b = opl_block(note);
    if (lo) *lo = f & 0xFF;
    return (on ? 0x20 : 0x00) | (b << 2) | ((f >> 8) & 3);
}

static inline int opl_detected(unsigned char st1, unsigned char st2)
{
    return (st1 & 0xE0) == 0x00 && (st2 & 0xE0) == 0xC0;
}
