static Paint *size_owner;
static AppField size_fields[2];
static char size_text[2][12], size_error[64];
static int size_focus;

static int size_number(const char *text, int limit)
{
    int n=0;
    if(!*text)return 0;
    for(;*text;text++){
        if(*text<'0'||*text>'9'||n>(limit-(*text-'0'))/10)return 0;
        n=n*10+*text-'0';
    }
    return n;
}

static void size_finish(int apply)
{
    Paint *p=size_owner;
    if(!p)return;
    if(apply){
        int w=size_number(size_text[0],PCW_MAX),h=size_number(size_text[1],PCH_MAX);
        if(!w||!h){strlcpy(size_error,"Enter dimensions from 1 to 4096.",sizeof size_error);api->gui_dirty();return;}
        if(!paint_set_size(p,w,h)){strlcpy(size_error,p->msg,sizeof size_error);api->gui_dirty();return;}
        p->ox=p->oy=0;view_clamp(p);
        win_fit_client(WT_PAINT,(int)(p-paints),paint_fit_w(p),paint_fit_h(p));
        kfmt(p->msg,sizeof p->msg,"Canvas: %d x %d pixels",p->cw,p->ch);
    }
    p->pending=0;size_owner=0;
    api->set_overlay(0,0);api->gui_dirty();
}

static void size_draw(void)
{
    int x=(SW-304)/2,y=(SH-188)/2;
    panel(x,y,304,188,0);
    fill_rect(x+2,y+2,300,18,C_TB0+4);
    draw_text(x+8,y+3,"Custom dimensions",C_WHITE);
    for(int i=0;i<2;i++){
        draw_text(x+12,y+36+i*32,i?"Height":"Width",C_BLACK);
        af_draw(&size_fields[i],x+92,y+32+i*32,120,size_focus==i);
        draw_text(x+220,y+36+i*32,"pixels",C_BLACK);
    }
    draw_text(x+12,y+96,"1-4096 each; available RAM required.",C_DARK);
    draw_text(x+12,y+112,"Smaller sizes crop the picture.",C_DARK);
    draw_text_clip(x+12,y+132,size_error,C_RED,280);
    for(int i=0;i<2;i++){
        button_label(api,x+76+i*80,y+158,72,22,i?"Cancel":"OK",0,1);
        if(size_focus==i+2)api->rect(x+78+i*80,y+160,68,18,C_BLACK);
    }
}

static int size_key(int k)
{
    if(k==27){size_finish(0);return 1;}
    if(k=='\t')size_focus=(size_focus+((api->kbd_mods()&1)?3:1))%4;
    else if(k=='\n'||k=='\r'||(k==' '&&size_focus>=2))size_finish(size_focus!=3);
    else if(size_focus<2){af_key(&size_fields[size_focus],k);size_error[0]=0;}
    api->gui_dirty();return 1;
}

static int size_mouse(int px,int py,int ev)
{
    int x=(SW-304)/2,y=(SH-188)/2;
    for(int i=0;i<2;i++)if(af_mouse(&size_fields[i],x+92,y+32+i*32,120,px,py,ev))size_focus=i;
    if(ev==EV_PRESS&&py>=y+158&&py<y+180){
        if(px>=x+76&&px<x+148)size_finish(1);
        else if(px>=x+156&&px<x+228)size_finish(0);
    }
    api->gui_dirty();return 1;
}

static void paint_custom(Paint *p)
{
    if(size_owner)return;
    size_owner=p;p->pending=-1;size_focus=0;size_error[0]=0;
    for(int i=0;i<2;i++){
        char value[12];kfmt(value,sizeof value,"%d",i?p->ch:p->cw);
        af_set(&size_fields[i],size_text[i],sizeof size_text[i],value);
        size_fields[i].anchor=0;
    }
    api->set_overlay(size_draw,size_mouse);api->set_overlay_key(size_key);api->gui_dirty();
}
