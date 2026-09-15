typedef struct { u8 *data; int bytes,cap,x,y,w,h,step; } WinImage;
static WinImage win_images[MAXWIN];
static u32 win_image_bytes;
static void win_image_free(int i)
{
    if(win_images[i].data){win_image_bytes-=win_images[i].cap;kfree(win_images[i].data);memset(&win_images[i],0,sizeof win_images[i]);}
}
void win_image_trim(void)
{
    for(int i=0;i<MAXWIN;i++)if(!app_handler_running(i))win_image_free(i);
}
static int win_image_encode(u8 *out,int cap,int x,int y,int w,int h,int step)
{
    int n=0;
    for(int yy=0;yy<h;yy+=step)for(int xx=0;xx<w;){
        u8 col=BACKBUF[(y+yy)*SW+x+xx];int run=1;xx+=step;
        while(xx<w&&run<255&&BACKBUF[(y+yy)*SW+x+xx]==col){run++;xx+=step;}
        if(n+2>cap)return -1;
        if(out){out[n]=run;out[n+1]=col;}n+=2;
    }
    return n;
}
static void win_image_save(int i)
{
    Win *v=&wins[i];WinImage *c=&win_images[i];
    int x=CLIX(v),y=CLIY(v),w=CLIW(v),h=CLIH(v),ox=0,oy=0;
    if(x<0){ox=-x;w+=x;x=0;}if(y<0){oy=-y;h+=y;y=0;}
    if(x+w>SW)w=SW-x;if(y+h>SH-TBH)h=SH-TBH-y;
    if(w<=0||h<=0)return;
    int limit=BOOTINFO->mem_kb<9216?8192:24576,step=1,n;
    do{n=win_image_encode(0,limit,x,y,w,h,step);if(n>=0)break;step*=2;}while(step<=16);
    if(n<0)return;
    if(c->cap<n){
        u32 budget=BOOTINFO->mem_kb<9216?32768:131072;
        win_image_free(i);
        for(int j=0;j<MAXWIN&&win_image_bytes+(u32)n>budget;j++)if(j!=i&&!app_handler_running(j))win_image_free(j);
        if(win_image_bytes+(u32)n>budget)return;
        c->data=kmalloc(n);if(!c->data)return;c->cap=n;win_image_bytes+=n;
        mem_track("Window snapshot",c->data,n);
    }
    c->x=ox;c->y=oy;c->w=w;c->h=h;c->step=step;
    c->bytes=win_image_encode(c->data,c->cap,x,y,w,h,step);
}
static int win_image_draw(int i)
{
    WinImage *c=&win_images[i];if(!c->data||c->bytes<=0)return 0;
    int x=0,y=0;
    for(int at=0;at+1<c->bytes&&y<c->h;at+=2){
        int w=c->data[at]*c->step,h=c->step;
        if(w>c->w-x)w=c->w-x;if(h>c->h-y)h=c->h-y;
        fill_rect(CLIX(&wins[i])+c->x+x,CLIY(&wins[i])+c->y+y,w,h,c->data[at+1]);
        x+=w;if(x>=c->w){x=0;y+=c->step;}
    }
    return 1;
}
