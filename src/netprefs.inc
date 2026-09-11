/* Stores network preferences in reserved settings bytes. */
#pragma once
#define NP_MAGIC 0x3154454eu
#define NP_FIELDS 8
typedef struct __attribute__((packed)) {
    u32 magic, dns, dns2;
    u16 isa_io, mtu;
    u8 adapter, dns_manual;
    char hostname[32];
} NetPrefs;
static inline int np_hostname(const char *s)
{
    int n = 0;
    for (; n < 32 && s[n]; n++) {
        char c = s[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || (c == '-' && n))) return 0;
    }
    return n < 32 && (!n || s[n - 1] != '-');
}
static inline void np_load(const FCfg *cfg, NetPrefs *p)
{
    for (unsigned i = 0; i < sizeof *p; i++) ((u8 *)p)[i] = cfg->reserved[i];
    if (p->magic != NP_MAGIC) {
        for (unsigned i = 0; i < sizeof *p; i++) ((u8 *)p)[i] = 0;
        p->magic = NP_MAGIC;
    }
    if (p->adapter > 5) p->adapter = 0;
    if (p->dns_manual > 1) p->dns_manual = 0;
    if (p->mtu < 1280 || p->mtu > 1500) p->mtu = 1500;
    if (p->isa_io && (p->isa_io < 0x200 || p->isa_io > 0x3e0 || (p->isa_io & 31))) p->isa_io = 0;
    p->hostname[31] = 0;
    if (!np_hostname(p->hostname)) p->hostname[0] = 0;
}
static inline void np_store(FCfg *cfg, const NetPrefs *p)
{
    for (unsigned i = 0; i < sizeof *p; i++) cfg->reserved[i] = ((const u8 *)p)[i];
}
