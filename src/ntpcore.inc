/* Converts NTP timestamps to calendar dates and times. */
#pragma once

typedef struct { int y, mo, d, h, mi, s; } NtpTime;

static inline int ntp_civil(u32 ntp_secs, int tz_qh, NtpTime *out)
{

    u32 t = ntp_secs + (u32)(tz_qh * 900);
    u32 days = t / 86400;
    u32 rem  = t % 86400;
    out->h  = (int)(rem / 3600);
    out->mi = (int)(rem % 3600 / 60);
    out->s  = (int)(rem % 60);
    int y = 1900;
    for (;;) {
        int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        u32 yd = leap ? 366u : 365u;
        if (days < yd) break;
        days -= yd;
        if (++y > 2100) return 0;
    }
    static const u8 mlen[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int mo = 0;
    for (;;) {
        u32 ml = (u32)mlen[mo] + ((mo == 1 && leap) ? 1u : 0u);
        if (days < ml) break;
        days -= ml;
        mo++;
    }
    out->y = y; out->mo = mo + 1; out->d = (int)days + 1;
    return (y >= 2000 && y <= 2099) ? 1 : 0;
}
