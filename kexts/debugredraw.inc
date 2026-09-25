static u8 redraw_pixels[2*(1024+768)];
static int redraw_x,redraw_y,redraw_w,redraw_h;

static void redraw_restore(void)
{
    int x=redraw_x,y=redraw_y,w=redraw_w,h=redraw_h;
    if(!h)return;
    api->blit(x,y,w,1,redraw_pixels,w);
    api->blit(x,y+h-1,w,1,redraw_pixels+w,w);
    api->blit(x,y,1,h,redraw_pixels+2*w,1);
    api->blit(x+w-1,y,1,h,redraw_pixels+2*w+h,1);
    redraw_h=0;
}

static void redraw_outline(int win)
{
    int sw=*api->screen_w,sh=*api->screen_h,x=0,y=0,w=sw,h=sh;
    if(win>=0){
        const Win *r=api->win_slot(win);if(!r)return;
        x=r->x;y=r->y;w=r->w;h=r->h;
    }
    if(x<0||y<0||w<=0||h<=0||x>sw||y>sh||w>sw-x||h>sh-y||
       w>1024||h>768)return;
    api->read_rect(x,y,w,1,redraw_pixels,w);
    api->read_rect(x,y+h-1,w,1,redraw_pixels+w,w);
    api->read_rect(x,y,1,h,redraw_pixels+2*w,1);
    api->read_rect(x+w-1,y,1,h,redraw_pixels+2*w+h,1);
    redraw_x=x;redraw_y=y;redraw_w=w;redraw_h=h;
    api->rect(x,y,w,h,win<0?C_YELLOW:C_BGREEN);
}
