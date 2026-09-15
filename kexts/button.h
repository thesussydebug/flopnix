#ifndef FLOPNIX_BUTTON_H
#define FLOPNIX_BUTTON_H
#include "menushade.h"
static inline int button_face(const Kapi *k,int x,int y,int w,int h,int selected,int enabled)
{
    int state=enabled&&k->version>=34&&k->control_state?k->control_state(x,y,w,h):0;
    int down=enabled&&(selected||(state&2));
    k->panel(x,y,w,h,down);
    if(enabled&&(selected||state))menu_shade(k,x+2,y+2,w-4,h-4,selected||(state&2));
    return down?2:state?1:0;
}
static inline void button_label(const Kapi *k,int x,int y,int w,int h,const char *label,int selected,int enabled)
{
    int state=button_face(k,x,y,w,h,selected,enabled),down=state==2;
    int tx=x+(w-k->text_width(label))/2+down,ty=y+(h-16)/2+1+down;
    if(!enabled)k->draw_text(tx+1,ty+1,label,C_LIGHT);
    k->draw_text(tx,ty,label,!enabled?C_G0+3:down?C_WHITE:C_BLACK);
}
#endif
