#include "button.h"
#define FP_COUNT MAXINST
typedef struct { char name[72], type[40], size[48], modified[40], location[136]; } FileProps;
static FileProps fp_data[FP_COUNT];
static int fp_type = -1;
static u8 fp_pressed[FP_COUNT];

static void fp_size(int inst, int *w, int *h)
{ (void)inst; *w = 336; *h = 150; }
static void fp_draw(Win *w, int x, int y, int cw, int ch)
{
    const FileProps *p = &fp_data[w->inst];
    api->fill_rect(x,y,cw,ch,C_FACE);
    menu_shade(api,x,y,cw,28,0);
    api->hline(x,y+27,cw,C_SHAD);
    const char *lines[] = {p->name,p->type,p->size,p->modified,p->location};
    for (int i=0;i<5;i++)
        api->draw_text_clip(x+10,y+10+i*22,lines[i],i ? C_BLACK : C_NAVY,cw-20);
    button_label(api,x+cw-78,y+ch-28,68,22,"Close",0,1);
}
static void fp_key(int inst, int k)
{ if (k==27 || k=='\r' || k=='\n') api->win_close_self(fp_type,inst); }
static void fp_mouse(int inst,int x,int y,int ev,int cw,int ch)
{
    int inside=x>=cw-78 && x<cw-10 && y>=ch-28 && y<ch-6;
    if(ev==EV_PRESS)fp_pressed[inst]=inside;
    if(ev==EV_RELEASE){
        int close=fp_pressed[inst]&&inside;
        fp_pressed[inst]=0;
        if(close)api->win_close_self(fp_type,inst);
    }
}
static void fp_register(void)
{
    static const AppDesc props = {
        .title="Properties", .max_inst=FP_COUNT, .draw=fp_draw,
        .client_size=fp_size, .key=fp_key, .mouse=fp_mouse,
    };
    fp_type=api->register_app(&props);
}
static void fp_show(const char *name,const char *type,u32 size,u32 modified,
                    const char *location,int directory)
{
    if (fp_type<0) { api->notify("Unable to open Properties"); return; }
    int count=0;
    for (int i=0;i<api->win_max();i++) {
        const Win *w=api->win_slot(i);
        if (w && w->used && w->type==fp_type) count++;
    }
    if (count>=FP_COUNT) { api->notify("Close a Properties window first"); return; }

    int inst=api->win_open(fp_type);
    if (inst<0) { api->notify("Close a Properties window first"); return; }
    FileProps *p=&fp_data[inst];
    char hs[16], mt[18];
    api->human_size(size,hs,sizeof hs); api->dos_fmt(modified,mt);
    api->strlcpy(p->name,name,sizeof p->name);
    api->kfmt(p->type,sizeof p->type,"Type: %s",directory ? "Folder" : type);
    if (directory) p->size[0]=0;
    else api->kfmt(p->size,sizeof p->size,"Size: %s (%u bytes)",hs,size);
    api->kfmt(p->modified,sizeof p->modified,"Modified: %s",mt);
    api->kfmt(p->location,sizeof p->location,"Location: %s",location);
    api->gui_dirty();
}
