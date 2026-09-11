/* Calls for the OPL2 sound service. */
#pragma once
#include "kapi.h"

#define FM_ABI      1
#define FM_VOICES   9

typedef struct {
    u32 abi;

    const char *(*name)(void);
    u16 (*port)(void);

    void (*note_on)(int voice, int note, int vel);
    void (*note_off)(int voice);
    void (*all_off)(void);

    void (*set_volume)(int atten);
} FmOps;

static inline const FmOps *fm_bind(const Kapi *k, u32 min_abi)
{
    const FmOps *f = (const FmOps *)k->service_get("fm");
    return (f && f->abi >= min_abi) ? f : 0;
}
