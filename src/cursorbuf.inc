typedef struct { u8 data[17*23]; int x,y,w,h; } CursorBuf;

static void cursorbuf_restore(CursorBuf *c,u8 *pixels,int pitch)
{
    for(int y=0;y<c->h;y++)memcpy(pixels+(c->y+y)*pitch+c->x,c->data+y*c->w,c->w);
    c->h=0;
}

static void cursorbuf_save(CursorBuf *c,const u8 *pixels,int pitch,int width,int height,int x,int y)
{
    int x0=x-6,y0=y-6,x1=x+11,y1=y+17;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>width)x1=width;if(y1>height)y1=height;
    c->x=x0;c->y=y0;c->w=x1-x0;c->h=y1-y0;
    if(c->w<=0||c->h<=0){c->h=0;return;}
    for(int row=0;row<c->h;row++)memcpy(c->data+row*c->w,pixels+(y0+row)*pitch+x0,c->w);
}
