/* Reads network link speed and duplex state. */
#pragma once

typedef struct {
    u8 present;
    u8 up;
    u8 autoneg_done;
    u8 full;
    u16 mbps;
} PhyState;

static inline void phy_decode(u16 bmsr, u16 anar, u16 anlpar, u16 bmcr,
                              PhyState *p)
{
    p->present = 0; p->up = 0; p->autoneg_done = 0; p->full = 0; p->mbps = 0;

    if (bmsr == 0xFFFF || bmsr == 0x0000) return;
    p->present = 1;
    if (!(bmsr & 0x0004)) return;
    p->up = 1;
    p->autoneg_done = (bmsr & 0x0020) ? 1 : 0;

    if (!p->autoneg_done) {

        p->mbps = (bmcr & 0x2000) ? 100 : 10;
        p->full = (bmcr & 0x0100) ? 1 : 0;
        return;
    }

    u16 both = (u16)(anar & anlpar);
    if      (both & 0x0100) { p->mbps = 100; p->full = 1; }
    else if (both & 0x0200) { p->mbps = 100; p->full = 0; }
    else if (both & 0x0080) { p->mbps = 100; p->full = 0; }
    else if (both & 0x0040) { p->mbps = 10;  p->full = 1; }
    else if (both & 0x0020) { p->mbps = 10;  p->full = 0; }

}

static inline u32 tul_csr6(int full_duplex, int running)
{
    u32 v = 0x00040000u;
    if (full_duplex) v |= 0x0200u;
    if (running)     v |= 0x2000u | 0x0002u;
    return v;
}
