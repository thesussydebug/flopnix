/* Encodes CPU descriptors and gates. */
#pragma once

#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UCODE 0x18
#define SEL_UDATA 0x20
#define SEL_TSS   0x28
#define SEL_DFTSS 0x30
#define GDT_ENTS  7

#define SEL_USER(s) ((s) | 3)

#define ACC_PRESENT 0x80
#define ACC_DPL(n)  ((n) << 5)
#define ACC_SYS     0x10
#define ACC_EXEC    0x08
#define ACC_RW      0x02
#define ACC_TSS32   0x09

#define ACC_KCODE (ACC_PRESENT | ACC_DPL(0) | ACC_SYS | ACC_EXEC | ACC_RW)
#define ACC_KDATA (ACC_PRESENT | ACC_DPL(0) | ACC_SYS | ACC_RW)
#define ACC_UCODE (ACC_PRESENT | ACC_DPL(3) | ACC_SYS | ACC_EXEC | ACC_RW)
#define ACC_UDATA (ACC_PRESENT | ACC_DPL(3) | ACC_SYS | ACC_RW)
#define ACC_TSS   (ACC_PRESENT | ACC_DPL(0) | ACC_TSS32)

#define GRAN_4G   0xC
#define GRAN_BYTE 0x0

static inline unsigned long long r3_desc(unsigned base, unsigned limit,
                                         unsigned access, unsigned gran)
{
    unsigned long long d = 0;
    d |= (unsigned long long)(limit & 0xFFFF);
    d |= (unsigned long long)(base & 0xFFFFFF) << 16;
    d |= (unsigned long long)(access & 0xFF) << 40;
    d |= (unsigned long long)((limit >> 16) & 0xF) << 48;
    d |= (unsigned long long)(gran & 0xF) << 52;
    d |= (unsigned long long)((base >> 24) & 0xFF) << 56;
    return d;
}

static inline unsigned r3_gate_flags(int dpl)
{
    return 0x80 | ((dpl & 3) << 5) | 0x0E;
}

#define R3_EFLAGS 0x00000202u

#define PF_PRESENT 0x1
#define PF_WRITE   0x2
#define PF_USER    0x4

static inline int pf_from_user(unsigned err) { return (err & PF_USER) != 0; }
