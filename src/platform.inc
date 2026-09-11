/* Identifies known hardware and emulator signatures. */
#define PCI_VEN_INTEL 0x8086u
#define PCI_DEV_I440FX 0x1237u
#define PCI_DEV_I430FX 0x122Du
#define PCI_DEV_Q35    0x29C0u

enum { PLAT_REAL = 0, PLAT_EMULATED = 1 };

static inline int plat_from_hostbridge(u32 id)
{
    u32 ven = id & 0xFFFFu, dev = (id >> 16) & 0xFFFFu;
    if (ven == 0xFFFFu) return PLAT_REAL;
    if (ven != PCI_VEN_INTEL) return PLAT_REAL;
    return (dev == PCI_DEV_I440FX || dev == PCI_DEV_I430FX ||
            dev == PCI_DEV_Q35) ? PLAT_EMULATED : PLAT_REAL;
}
