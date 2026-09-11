/* Checks emergency display bounds and watchdog time limits. */
#pragma once
#define EM_WATCH_TICKS 3000u
#define EM_PANIC_TICKS 500u
typedef struct { u32 base, width, height, pitch, bank, check; } EmergencyVideo;
static inline u32 em_video_check(const EmergencyVideo *v)
{ return ~(v->base ^ v->width ^ v->height ^ v->pitch ^ v->bank ^ 0x50414e49u); }
static inline int em_video_valid(const EmergencyVideo *v)
{
    if (v->check != em_video_check(v) || v->width < 320 || v->width > 2048 ||
        v->height < 200 || v->height > 1536 || v->pitch < v->width || v->pitch > 8192 || v->bank > 4) return 0;
    u32 bytes=v->pitch*v->height;
    if (v->bank) return v->base==0xa0000u;
    if (v->base==0xa0000u) return bytes<=65536u;
    return v->base>=0x1000000u && v->base<=0xffffffffu-bytes;
}
static inline int em_watch_step(u32 *age, int armed, int panic)
{
    if (!armed) return 0;
    u32 limit=panic ? EM_PANIC_TICKS : EM_WATCH_TICKS;
    if (*age < limit) ++*age;
    return *age >= limit;
}
