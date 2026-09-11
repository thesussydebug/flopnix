#include "textfield.inc"
static void af_set(TextField *t,char *buf,int cap,const char *value)
{
    api->strlcpy(buf,value,cap);t->buf=buf;t->cap=cap;t->len=(int)api->strlen(buf);t->caret=t->len;t->all=0;
}
static void af_key(TextField *t,int k)
{
    if(k==1){t->all=1;return;}
    if(k==3){api->clip_set_text(t->buf);return;}
    if(k==22){char p[256];if(api->clip_get_text(p,sizeof p)>0)for(int i=0;p[i];i++)if((u8)p[i]>=32&&(u8)p[i]<127)tf_key(t,p[i],1);return;}
    tf_key(t,k,k>=32&&k<127);
}
static void af_draw(TextField *t,int x,int y,int width,int focused)
{
    api->panel(x,y,width,24,1);api->fill_rect(x+2,y+2,width-4,20,C_WHITE);
    int cols=(width-12)/8,off=t->caret-cols+1;if(off<0)off=0;
    if(focused&&t->all)api->fill_rect(x+3,y+3,width-6,18,C_NAVY);
    api->draw_text_clip(x+6,y+4,t->buf+off,focused&&t->all?C_WHITE:C_BLACK,width-12);
    if(focused&&!t->all)api->vline(x+6+(t->caret-off)*8,y+4,15,C_BLACK);
}
