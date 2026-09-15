#ifndef FLOPNIX_MENUSHADE_H
#define FLOPNIX_MENUSHADE_H
#include "gdi.h"
static void menu_shade(const Kapi *k,int x,int y,int w,int h,int selected)
{
    if (w<=0 || h<=0) return;
    const GdiOps *g=gdi_bind(k,11);
    if (g && g->fill_gradient && g->set_dither) {
        g->set_dither(1);
        g->fill_gradient(x,y,w,h,
            selected ? GRGB(50,100,176) : GRGB(214,218,228),
            selected ? GRGB(22,48,100) : GRGB(180,186,202),1);
        g->set_dither(0);
    } else k->fill_rect(x,y,w,h,selected ? C_NAVY : C_FACE);
}
#endif
