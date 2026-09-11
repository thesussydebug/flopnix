/* Checks saved settings and replaces invalid values. */
#pragma once

#include "wallpaper.inc"

static inline void cfg_sanitize(FCfg *c)
{
    if (c->video < 1 || c->video > 4) c->video = 1;
    if (c->net_mode > 1) c->net_mode = 0;
    if (c->mouse_speed < 1 || c->mouse_speed > 4) c->mouse_speed = 2;
    if (c->ss_enable > 1) c->ss_enable = 1;
    if (c->ss_secs < 5 || c->ss_secs > 240) c->ss_secs = 30;
    if (c->wp_mode > WP_BITMAP) c->wp_mode = WP_GRADIENT;
    if (c->tz_qh < -48 || c->tz_qh > 56) c->tz_qh = 0;
    c->wp_path[sizeof c->wp_path - 1] = 0;
    for (const char *p = c->wp_path; *p; p++)
        if (*p < 32 || *p > 126) { c->wp_path[0] = 0; break; }
}
