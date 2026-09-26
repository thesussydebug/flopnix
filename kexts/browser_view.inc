#pragma once
static void view_free(BrView *v)
{
    for(int i=0;i<BR_OBJECTS;i++){BrMedia *m=&v->media[i];if(m->pixels){media_used-=(u32)(m->w*m->h);api->kfree(m->pixels);}if(m->child){view_free(m->child);api->kfree(m->child);}}
    if(v->doc){br_page_free(v->doc);api->kfree(v->doc);}if(v->layout){br_layout_free(v->layout);api->kfree(v->layout);}api->memset(v,0,sizeof *v);
}
static void view_reparent(BrView *v){for(int i=0;i<BR_OBJECTS;i++)if(v->media[i].child){v->media[i].child->parent=v;view_reparent(v->media[i].child);}}
static int resource_read(char *url,Transfer *d)
{
    fetch_status=0;if(closing||api->esc_pending()||resource_count>=32)return 0;resource_count++;
    char path[TW_URL];int n=0;while(url[n]&&url[n]!='#')n++;tw_copy(path,TW_URL,url,n);
    if(tw_local(path,path))return read_local(path,d);TwUrl u;if(!tw_url(url,&u)||!u.http)return 0;if(!allocate(d))return 0;return fetch(url,&u,d,0);
}
static int cacheable(const char *url){TwUrl u;return tw_url(url,&u)&&u.http;}
static int view_media(BrView *v,int oi,u8 *px,int dw,int dh,int w,int h)
{
    BrMedia *m=&v->media[oi];BrObject *o=&v->doc->object[oi];u32 size=(u32)(dw*dh);
    if(!px)px=br_malloc(size);if(!px)return 0;m->pixels=px;media_used+=size;m->w=dw;m->h=dh;
    if(!o->width&&!o->height){o->width=dw;o->height=dh;}else if(!o->width)o->width=br_max(1,o->height*w/h);else if(!o->height)o->height=o->width>0?br_max(1,o->width*h/w):dh;
    return 1;
}
static __attribute__((noinline)) int view_image(BrView *v,int oi,const Transfer *d)
{
    int w,h,c;u32 total=api->mem_total_kb();image_limit=total>=16256?4194304:total>=7936?1048576:262144;
    if(!stbi_info_from_memory(d->data,(int)d->len,&w,&h,&c)||w<1||h<1||(u32)w*(u32)h>image_limit/4)return 0;
    u8 *rgba=stbi_load_from_memory(d->data,(int)d->len,&w,&h,&c,4);if(!rgba)return -1;
    int dw=br_min(w,512),dh=br_max(1,h*dw/w);if(dh>512){dw=br_max(1,dw*512/dh);dh=512;}
    if(!view_media(v,oi,0,dw,dh,w,h)){stbi_image_free(rgba);return -1;}BrMedia *m=&v->media[oi];
    static u16 nearest[4096];for(int i=0;i<4096;i++)nearest[i]=256;
    for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){const u8 *p=rgba+((y*h/dh)*w+x*w/dw)*4;if(p[3]<128){m->pixels[y*dw+x]=255;continue;}int k=(p[0]>>4)*256+(p[1]>>4)*16+(p[2]>>4);if(nearest[k]==256){u8 index=api->palette_nearest(p[0],p[1],p[2]);nearest[k]=index==255?C_WHITE:index;}m->pixels[y*dw+x]=(u8)nearest[k];}
    stbi_image_free(rgba);
    const char *url=v->doc->object[oi].url;if(cacheable(url))bc_put(&cache,url,BC_IMAGE,m->pixels,(u32)(dw*dh),dw|dh<<16,w|h<<16);
    return 1;
}
static void view_picture(BrView *v,int i)
{
    const char *url=v->doc->object[i].url;if(!url[0])return;int keep=cacheable(url);
    BcEntry *e=keep&&!reload_all?bc_find(&cache,url,BC_IMAGE):0;
    if(e){int a=e->a,b=e->b;u32 n=e->size;u8 *px=br_malloc(n);
        if(px&&(e=bc_find(&cache,url,BC_IMAGE))&&e->size==n){api->memcpy(px,e->data,n);if(view_media(v,i,px,a&0xffff,a>>16,b&0xffff,b>>16))return;}
        api->kfree(px);}
    if(keep&&!reload_all&&bc_find(&cache,url,BC_FAILED))return;
    char req[TW_URL];tw_copy(req,TW_URL,url,tw_len(url));Transfer d;api->memset(&d,0,sizeof d);
    int got=resource_read(req,&d),r=got?view_image(v,i,&d):-1;release(&d);
    if(keep&&!v->media[i].pixels&&(r==0||(!got&&fetch_status>=400&&fetch_status<500)))bc_put(&cache,url,BC_FAILED,0,0,0,0);
}
static void view_css(const char *url,char **external,int *csslen)
{
    if(!*external){*external=br_malloc(BR_CSS_TEXT+1);if(!*external)return;**external=0;}
    int keep=cacheable(url);BcEntry *e=keep&&!reload_all?bc_find(&cache,url,BC_CSS):0;
    Transfer d;api->memset(&d,0,sizeof d);const u8 *data=0;u32 n=0;char req[TW_URL];
    if(e){data=e->data;n=e->size;}
    else if(tw_copy(req,TW_URL,url,tw_len(url))&&resource_read(req,&d)){data=d.data;n=d.len;if(keep)bc_put(&cache,url,BC_CSS,d.data,d.len,0,0);}
    int room=BR_CSS_TEXT-1-*csslen;if(data&&room>0){if(n>(u32)room)n=(u32)room;api->memcpy(*external+*csslen,data,n);*csslen+=(int)n;(*external)[(*csslen)++]='\n';(*external)[*csslen]=0;}
    release(&d);
}
static __attribute__((noinline)) void view_render(BrView *v,const char *source,int mode,BrCss *css);
static __attribute__((noinline)) int view_load(BrView *v,const char *url,const char *body)
{
    Transfer d;api->memset(&d,0,sizeof d);char requested[TW_URL],localpath[TW_URL];TwUrl u;tw_copy(requested,TW_URL,url,tw_len(url));int n=0;while(requested[n]&&requested[n]!='#')n++;tw_copy(localpath,TW_URL,requested,n);int local=tw_local(localpath,localpath);
    if(body&&local)return 0;if(!local&&!tw_url(requested,&u))return 0;
    if(!(local?read_local(localpath,&d):(allocate(&d)&&fetch(requested,&u,&d,body)))){release(&d);return 0;}
    BrView *next;
    for(int hops=0;;hops++){
        int mode=local?1:br_mode(&u,&d.info,d.data,(int)d.len);if(mode<0){say("This frame response is a file. Use Download to save it.");release(&d);return 0;}
        br_text_fix(d.data,d.len);
        next=br_zalloc(sizeof *next);if(!next){release(&d);return 0;}next->parent=v->parent;next->depth=v->depth;next->post=body!=0;next->doc=br_zalloc(sizeof *next->doc);next->layout=br_zalloc(sizeof *next->layout);BrCss *css=br_malloc(sizeof *css);
        if(!next->doc||!next->layout||!css){view_free(next);api->kfree(next);api->kfree(css);release(&d);return 0;}next->layout->width=0;tw_copy(next->url,TW_URL,requested,tw_len(requested));view_render(next,(const char *)d.data,mode,css);api->kfree(css);
        if(closing||follow_refresh(next->doc,hops,requested,localpath,&local,&u,&d)<=0)break;
        view_free(next);api->kfree(next);body=0;
    }
    release(&d);
    if(closing){view_free(next);api->kfree(next);return 0;}formatting=1;nav_serial++;view_free(v);api->memcpy(v,next,sizeof *v);api->kfree(next);view_reparent(v);formatting=0;return 1;
}
static __attribute__((noinline)) void view_render(BrView *v,const char *source,int mode,BrCss *css)
{
    char *external=0;int csslen=0,count=0;char cssbase[TW_URL];tw_copy(cssbase,sizeof cssbase,v->url,tw_len(v->url));
    if(mode==1)for(int p=0;source[p]&&count<4;){if(source[p]!='<'){p++;continue;}if(source[p+1]=='!'&&source[p+2]=='-'&&source[p+3]=='-'){p+=4;while(source[p]&&!(source[p]=='-'&&source[p+1]=='-'&&source[p+2]=='>'))p++;if(source[p])p+=3;continue;}char tag[12],rel[80],href[TW_URL],url[TW_URL];int e,close,a;if(!br_tag(source,p,&e,tag,&close,&a))break;p=e+1;
        if(!close&&(br_same(tag,"script")||br_same(tag,"style"))){br_content_end(source,e+1,tag,&p);continue;}
        if(!close&&br_same(tag,"base")&&br_attr(source+a,e-a,"href",href,sizeof href)&&br_resolve(cssbase,href,url))tw_copy(cssbase,TW_URL,url,tw_len(url));
        if(close||!br_same(tag,"link")||!br_attr(source+a,e-a,"rel",rel,sizeof rel)||!br_word(rel,"stylesheet",10)||!br_attr(source+a,e-a,"href",href,sizeof href)||!br_resolve(cssbase,href,url))continue;count++;view_css(url,&external,&csslen);
    }
    br_render_extra(v->doc,source,mode,v->url,css,external);api->kfree(external);
    for(int i=0;i<v->doc->objects&&!closing&&!api->esc_pending();i++){
        BrObject *o=&v->doc->object[i];if(o->kind==BR_IMAGE){view_picture(v,i);if(!v->media[i].pixels)resource_failures++;}
        else if(o->kind==BR_FRAME){if(v->depth>=2||document_count>=4||!o->url[0]){resource_failures++;continue;}int loop=0;for(BrView *a=v;a;a=a->parent)if(tw_eq(a->url,o->url))loop=1;if(loop){resource_failures++;continue;}
            BrView *child=br_zalloc(sizeof *child);if(!child){resource_failures++;continue;}child->parent=v;child->depth=v->depth+1;document_count++;resource_count++;if(view_load(child,o->url,0))v->media[i].child=child;else {view_free(child);api->kfree(child);resource_failures++;}}
    }
}
typedef struct {int x,y,w,h;} BrClip;
static BrClip view_clip(BrClip a,BrClip b){int x=br_max(a.x,b.x),y=br_max(a.y,b.y),r=br_min(a.x+a.w,b.x+b.w),d=br_min(a.y+a.h,b.y+b.h);return (BrClip){x,y,br_max(0,r-x),br_max(0,d-y)};}
static void view_fill(BrClip clip,int x,int y,int w,int h,u8 color){BrClip r=view_clip(clip,(BrClip){x,y,w,h});if(r.w>0&&r.h>0)api->fill_rect(r.x,r.y,r.w,r.h,color);}
static void view_border(BrClip clip,int x,int y,int w,int h,u8 color){view_fill(clip,x,y,w,1,color);view_fill(clip,x,y+h-1,w,1,color);view_fill(clip,x,y,1,h,color);view_fill(clip,x+w-1,y,1,h,color);}
static void view_char(BrClip clip,int x,int y,int c,u8 color){if(x>=clip.x&&y>=clip.y&&x+8<=clip.x+clip.w&&y+16<=clip.y+clip.h)api->draw_char(x,y,(char)c,color);}
static void view_text(BrClip clip,int x,int y,const char *s,u8 color,int width){for(int i=0;s[i]&&i*8+8<=width;i++)view_char(clip,x+i*8,y,s[i],color);}
static void view_layout(BrView *v,int width)
{
    if(!v->doc||!v->layout)return;
    if(v->layout->width!=width){for(int i=0;i<v->doc->objects;i++){BrObject *o=&v->doc->object[i];if(o->kind==BR_FRAME&&o->ref>=0&&v->viewport){BrTable *t=&v->doc->table[o->ref];o->height=br_max(48,v->viewport/br_max(1,t->vertical?t->rows:1)-2*t->padding-2*t->spacing-4);}}br_layout(v->doc,v->layout,width);}
}
static void view_control(BrView *v,int ci,BrClip clip,int x,int y,int w,int h)
{
    BrControl *c=&v->doc->control[ci];BrObject *o=&v->doc->object[c->object];int focused=active_view==v&&active_control==ci;u8 fg=c->disabled?C_GRAY:C_BLACK;
    view_fill(clip,x,y,w,h,c->type==BC_SUBMIT||c->type==BC_RESET||c->type==BC_BUTTON?C_LIGHT:C_WHITE);view_border(clip,x,y,w,h,focused?C_BBLUE:C_GRAY);
    if(c->type==BC_CHECK||c->type==BC_RADIO){view_text(clip,x+3,y+4,c->checked?(c->type==BC_RADIO?"o":"x"):" ",fg,w-4);return;}
    if(c->type==BC_SUBMIT||c->type==BC_RESET||c->type==BC_BUTTON||c->type==BC_FILE){view_text(clip,x+8,y+4,o->label,fg,w-12);return;}
    if(c->type==BC_SELECT){if(c->multiple){for(int row=0;row<5;row++){int k=br_max(0,c->choice)/5*5+row;if(k>=c->count)break;BrOption *opt=&v->doc->option[c->option+k];view_text(clip,x+4,y+2+row*20,opt->selected?"[x]":"[ ]",fg,24);view_text(clip,x+30,y+2+row*20,opt->label,opt->disabled?C_GRAY:fg,w-34);}}else {if(c->choice>=0)view_text(clip,x+5,y+4,v->doc->option[c->option+c->choice].label,fg,w-24);view_text(clip,x+w-16,y+4,"v",fg,8);}return;}
    int cols=br_max(1,(w-12)/8),rows=br_max(1,(h-8)/16),start=0;
    if(focused){if(c->type!=BC_AREA)start=af_offset(&control_field,w);else {int line=0,col=0;for(int i=0;i<control_field.caret;i++){if(c->value[i]=='\n'||++col>=cols){line++;col=0;}}int skip=br_max(0,line-rows+1);for(int i=0;i<skip;i++)start=tw_wrap(c->value,start,cols);}}
    int row=0,col=0,len=tw_len(c->value);
    for(int p=start;p<=len&&row<rows;p++){
        if(focused&&p==control_field.caret&&*api->gui_blink)view_fill(clip,x+5+col*8,y+4+row*16,1,15,fg);
        if(p==len)break;int ch=c->value[p];if(ch=='\n'&&c->type==BC_AREA){row++;col=0;continue;}int sel=focused&&p>=br_min(control_field.caret,control_field.anchor)&&p<br_max(control_field.caret,control_field.anchor);
        if(sel)view_fill(clip,x+5+col*8,y+4+row*16,8,16,C_NAVY);view_char(clip,x+5+col*8,y+4+row*16,c->type==BC_PASSWORD?'*':ch,sel?C_WHITE:fg);
        if(++col>=cols){if(c->type!=BC_AREA)break;row++;col=0;}
    }
}
static void view_draw(BrView *v,int x,int y,int w,int h,BrClip clip)
{
    if(!v->doc||!v->layout)return;if(v->viewport!=h){v->viewport=h;v->layout->width=0;}view_layout(v,w);v->scroll=br_max(0,br_min(v->scroll,v->layout->height-h));clip=view_clip(clip,(BrClip){x,y,w,h});view_fill(clip,x,y,w,h,v->doc->background);
    for(int i=0;i<v->layout->count;i++){
        BrBox *b=&v->layout->box[i];int xx=x+b->x,yy=y+b->y-v->scroll;if(yy+b->h<=clip.y||yy>=clip.y+clip.h)continue;
        if(b->object<=-2){const BrCell *c=&v->doc->cell[-2-b->object];view_fill(clip,xx,yy,b->w,b->h,c->style.bg);if(v->doc->table[c->table].border)view_border(clip,xx,yy,b->w,b->h,c->style.fg);continue;}
        if(b->object<0){for(int p=(int)b->start;p<(int)b->end;p++){int ri=br_run_at(v->doc,p);BrStyle st=v->doc->runs?v->doc->run[ri].style:(BrStyle){0,15,0,0};int link=v->doc->runs?v->doc->run[ri].link:0,px=xx+(p-(int)b->start)*8;if(active_view==v&&link&&link==selected+1){st.fg=C_WHITE;st.bg=C_NAVY;}st.fg=(u8)br_contrast(st.fg,st.bg);view_fill(clip,px,yy,8,16,st.bg);view_char(clip,px,yy,v->doc->page.text[p],st.fg);if(st.flags&1)view_char(clip,px+1,yy,v->doc->page.text[p],st.fg);if(st.flags&2)view_fill(clip,px,yy+14,8,1,st.fg);}continue;}
        BrObject *o=&v->doc->object[b->object];BrMedia *m=&v->media[b->object];
        if(o->kind==BR_TABLE){view_fill(clip,xx,yy,b->w,b->h,o->style.bg);if(v->doc->table[o->ref].border)view_border(clip,xx,yy,b->w,b->h,o->style.fg);}
        else if(o->kind==BR_CONTROL)view_control(v,o->ref,clip,xx,yy,b->w,b->h);
        else if(o->kind==BR_RULE)view_fill(clip,xx,yy+3,b->w,1,o->style.fg);
        else if(o->kind==BR_FRAME&&m->child){view_border(clip,xx,yy,b->w,b->h,C_GRAY);int fw=br_max(8,b->w-18),fh=br_max(16,b->h-6);view_draw(m->child,xx+3,yy+3,fw,fh,clip);view_fill(clip,xx+b->w-13,yy+2,11,b->h-4,C_LIGHT);int total=br_max(fh,m->child->layout->height),thumb=br_max(8,(b->h-4)*fh/total),offset=(b->h-4-thumb)*m->child->scroll/br_max(1,total-fh);view_fill(clip,xx+b->w-12,yy+2+offset,9,thumb,C_GRAY);}
        else if(o->kind==BR_IMAGE&&m->pixels){u8 scan[512];int left=br_max(0,clip.x-xx),right=br_min(b->w,clip.x+clip.w-xx),from=br_max(0,clip.y-yy),to=br_min(b->h,clip.y+clip.h-yy);for(int row=from;row<to;row++){const u8 *line=m->pixels+(row*m->h/b->h)*m->w;for(int start=left;start<right;start+=512){int count=br_min(512,right-start);const u8 *px=line+start;if(m->w!=b->w){br_scale_row(scan,line,start,count,m->w,b->w);px=scan;}api->blit_key(xx+start,yy+row,count,1,px,count,255);}}}
        else {view_border(clip,xx,yy,b->w,b->h,C_GRAY);view_text(clip,xx+4,yy+4,o->label[0]?o->label:"Image unavailable",C_GRAY,b->w-8);}
    }
}
static BrBox *view_hit(BrView **out,int *mx,int *my,BrView *v,int x,int y,int w,int h)
{
    if(!v->layout||!v->doc||*mx<x||*my<y||*mx>=x+w||*my>=y+h)return 0;view_layout(v,w);int px=*mx-x,py=*my-y+v->scroll;
    for(int i=v->layout->count-1;i>=0;i--){BrBox *b=&v->layout->box[i];if(px<b->x||py<b->y||px>=b->x+b->w||py>=b->y+b->h||b->object<=-2)continue;
        if(b->object>=0&&v->doc->object[b->object].kind==BR_TABLE)continue;
        if(b->object>=0&&v->media[b->object].child){if(px>=b->x+b->w-14){BrView *child=v->media[b->object].child;child->scroll=api->sbar_from_pos(b->h-4,child->layout?child->layout->height:0,b->h-6,py-b->y-2);*out=child;return 0;}return view_hit(out,mx,my,v->media[b->object].child,x+b->x+3,y+b->y-v->scroll+3,br_max(8,b->w-18),br_max(16,b->h-6));}
        *out=v;*mx=px-b->x;*my=py-b->y;return b;
    }*out=v;return 0;
}
static BrView *view_named(BrView *v,const char *name){if(!v->doc)return 0;for(int i=0;i<v->doc->objects;i++)if(v->media[i].child){if(tw_eq(v->doc->object[i].name,name))return v->media[i].child;BrView *r=view_named(v->media[i].child,name);if(r)return r;}return 0;}
static BrView *view_target(BrView *v,const char *name){if(!name[0]||br_same(name,"_self"))return v;if(br_same(name,"_top")||br_same(name,"_blank"))return &root_view;if(br_same(name,"_parent"))return v->parent?v->parent:v;BrView *found=view_named(&root_view,name);return found?found:&root_view;}
static int view_anchor(BrView *v,const char *url)
{
    int n=0;while(url[n]&&url[n]!='#')n++;if(!url[n])return 0;int m=0;while(v->url[m]&&v->url[m]!='#')m++;if(n!=m)return 0;for(int i=0;i<n;i++)if(url[i]!=v->url[i])return 0;
    view_layout(v,v->layout->width?v->layout->width:532);v->scroll=0;for(int a=0;a<v->doc->anchors;a++)if(tw_eq(v->doc->anchor[a].name,url+n+1)){for(int i=0;i<v->layout->count;i++)if(v->layout->box[i].end>v->doc->anchor[a].off){v->scroll=v->layout->box[i].y;break;}break;}return 1;
}
