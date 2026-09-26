#pragma once
typedef struct {int form,depth,table[12],cell[12],row[12],column[12];} BrParse;
static int br_has(const char *s,int n,const char *key)
{
    for(int p=0;p<n;){while(p<n&&(br_space(s[p])||s[p]=='/'))p++;int a=p;while(p<n&&!br_space(s[p])&&s[p]!='='&&s[p]!='/')p++;int end=p;
        if(end-a==tw_len(key)&&tw_ci(s+a,key,end-a))return 1;
        while(p<n&&br_space(s[p]))p++;if(p<n&&s[p]=='='){p++;while(p<n&&br_space(s[p]))p++;int q=p<n&&(s[p]=='\''||s[p]=='"')?s[p++]:0;while(p<n&&(q?s[p]!=q:!br_space(s[p])))p++;if(q&&p<n)p++;}else if(p==a)p++;
    }return 0;
}
static int br_number(const char *s,int fallback,int max)
{
    if(*s<'0'||*s>'9')return fallback;int n=0;while(*s>='0'&&*s<='9'){n=n*10+*s++-'0';if(n>max)return max;}return n;
}
static int br_dimension(const char *s,int fallback,int max){int n=br_number(s,fallback,max);return tw_contains(s,"%")?-br_number(s,100,100):n;}
static int br_attrnum(const char *s,int n,const char *key,int fallback,int max){char v[32];return br_attr(s,n,key,v,sizeof v)?br_number(v,fallback,max):fallback;}
static int br_resolve(const char *base,const char *ref,char *out)
{
    if(!tw_resolve(base,ref,out))return 0;
    char local[TW_URL];if(tw_local(base,local)){int p=0;while(ref[p]&&ref[p]!='#')p++;if(ref[p]){int n=tw_len(out);if(n+tw_len(ref+p)>=TW_URL)return 0;tw_copy(out+n,TW_URL-n,ref+p,tw_len(ref+p));}}
    return 1;
}
static void br_anchor(BrPage *b,const char *s,int n,const char *tag)
{
    char name[64];if(!br_attr(s,n,"id",name,sizeof name)&&!(br_same(tag,"a")&&br_attr(s,n,"name",name,sizeof name)))return;
    if(!name[0]||b->anchors==BR_ANCHORS)return;BrAnchor *a=&b->anchor[b->anchors++];tw_copy(a->name,sizeof a->name,name,tw_len(name));a->off=(u32)b->page.len;
}
static void br_parse_init(BrParse *p){p->form=-1;p->depth=0;}
static int br_object(BrPage *b,int kind,BrStyle st,int link)
{
    if((st.flags&8)||b->objects==BR_OBJECTS||!tw_room(&b->page,1)){if(!(st.flags&8))b->page.clipped=1;return -1;}
    int i=b->objects++;BrObject *o=&b->object[i];o->kind=(u8)kind;o->link=(u16)link;o->style=st;o->width=o->height=0;o->ref=-1;o->url[0]=o->label[0]=o->name[0]=0;o->start=(u32)b->page.len;
    br_run(b,st,link);b->page.text[b->page.len++]=1;b->page.text[b->page.len]=0;o->end=(u32)b->page.len;return i;
}
static void br_endcell(BrPage *b,BrParse *p){if(p->depth&&p->cell[p->depth-1]>=0){b->cell[p->cell[p->depth-1]].end=(u32)b->page.len;p->cell[p->depth-1]=-1;}}
static int br_cell(BrPage *b,BrParse *p,const char *s,int n,BrStyle st)
{
    if(!p->depth||b->cells==BR_CELLS){b->page.clipped=1;return -1;}br_endcell(b,p);int d=p->depth-1;BrCell *c=&b->cell[b->cells];
    c->table=p->table[d];c->row=p->row[d];if(c->row<0)c->row=p->row[d]=0;c->col=p->column[d];c->span=br_attrnum(s,n,"colspan",1,8);if(c->span<1)c->span=1;c->rows=br_attrnum(s,n,"rowspan",1,32);if(c->rows<1)c->rows=1;
    c->start=c->end=(u32)b->page.len;c->style=st;char value[32];c->width=br_attr(s,n,"width",value,sizeof value)?br_dimension(value,0,2048):0;
    p->column[d]+=c->span;BrTable *t=&b->table[c->table];if(p->column[d]>t->cols)t->cols=p->column[d];if(c->row+c->rows>t->rows)t->rows=c->row+c->rows;
    return p->cell[d]=b->cells++;
}
static void br_close_element(BrPage *b,BrParse *p,const char *tag)
{
    if(br_same(tag,"form"))p->form=-1;
    if(br_same(tag,"td")||br_same(tag,"th")||br_same(tag,"tr"))br_endcell(b,p);
    if((br_same(tag,"table")||br_same(tag,"frameset"))&&p->depth){br_endcell(b,p);int t=p->table[--p->depth];b->object[b->table[t].object].end=(u32)b->page.len;br_break(b);}
}
static void br_finish_elements(BrPage *b,BrParse *p){while(p->depth)br_close_element(b,p,"table");}
static int br_content_end(const char *s,int start,const char *tag,int *after)
{
    for(int p=start;s[p];p++)if(s[p]=='<'&&s[p+1]=='/'){int e,c,a;char name[12];if(br_tag(s,p,&e,name,&c,&a)&&br_same(name,tag)){*after=e+1;return p;}}
    *after=tw_len(s);return *after;
}
static void br_plain(char *dst,int cap,const char *s,int n)
{
    char raw[256];int o=0;for(int i=0;i<n&&o<255;i++){if(s[i]=='<'){while(i<n&&s[i]!='>')i++;}else raw[o++]=s[i];}raw[o]=0;br_decode(dst,cap,raw,o);
}
static int br_open_element(BrPage *b,BrParse *p,const char *s,int *pos,int e,int a,const char *tag,BrStyle st,int link)
{
    const char *attrs=s+a;int n=e-a;char v[256];
    if(br_same(tag,"base")){if(br_attr(attrs,n,"href",v,sizeof v)){char url[TW_URL];if(br_resolve(b->base,v,url))tw_copy(b->base,TW_URL,url,tw_len(url));}*pos=e+1;return 1;}
    if(st.flags&8)return 0;
    if(br_same(tag,"form")){
        p->form=-1;if(b->forms<BR_FORMS){p->form=b->forms++;BrForm *f=&b->form[p->form];tw_copy(f->action,TW_URL,b->base,tw_len(b->base));
            if(br_has(attrs,n,"action")){if(!br_attr(attrs,n,"action",v,sizeof v)||(v[0]&&!br_resolve(b->base,v,f->action)))f->action[0]=0;}
            f->post=(u8)(br_attr(attrs,n,"method",v,sizeof v)&&br_same(v,"post"));f->unsupported=(u8)(br_attr(attrs,n,"enctype",v,sizeof v)&&!br_same(v,"application/x-www-form-urlencoded"));br_attr(attrs,n,"target",f->target,sizeof f->target);
        }else b->page.clipped=1;br_break(b);return 0;
    }
    int frameset=br_same(tag,"frameset");
    if(br_same(tag,"table")||frameset){
        br_break(b);if(p->depth==12||b->tables==BR_TABLES){b->page.clipped=1;return 0;}
        if(p->depth&&b->table[p->table[p->depth-1]].frame)br_cell(b,p,"",0,st);
        int oi=br_object(b,BR_TABLE,st,0);if(oi<0)return 0;int ti=b->tables++;BrTable *t=&b->table[ti];t->object=oi;t->cols=t->rows=0;t->border=br_attrnum(attrs,n,"border",frameset?1:0,8);t->padding=br_attrnum(attrs,n,"cellpadding",3,16);t->spacing=br_attrnum(attrs,n,"cellspacing",2,12);t->frame=frameset;t->vertical=0;t->sizes[0]=0;t->align=st.align;
        t->width=br_attr(attrs,n,"width",v,sizeof v)?br_dimension(v,0,2048):0;
        if(frameset){if(!br_attr(attrs,n,"cols",t->sizes,sizeof t->sizes)){br_attr(attrs,n,"rows",t->sizes,sizeof t->sizes);t->vertical=1;}t->cols=1;}
        b->object[oi].ref=ti;p->table[p->depth]=ti;p->cell[p->depth]=-1;p->row[p->depth]=frameset?0:-1;p->column[p->depth++]=0;return 0;
    }
    if(br_same(tag,"tr")&&p->depth){br_endcell(b,p);int d=p->depth-1;if(p->row[d]<31)p->row[d]++;else b->page.clipped=1;p->column[d]=0;return 0;}
    if(br_same(tag,"td")||br_same(tag,"th")){br_cell(b,p,attrs,n,st);return 0;}
    int frame=br_same(tag,"frame")||br_same(tag,"iframe");
    if(br_same(tag,"img")||frame||br_same(tag,"hr")){
        if(frame&&p->depth&&b->table[p->table[p->depth-1]].frame){int d=p->depth-1;BrTable *t=&b->table[p->table[d]];if(t->vertical){br_endcell(b,p);p->row[d]=p->column[d];p->column[d]=0;}br_cell(b,p,"",0,st);if(t->vertical)p->column[d]=p->row[d]+1;}
        int oi=br_object(b,frame?BR_FRAME:br_same(tag,"hr")?BR_RULE:BR_IMAGE,st,link);
        if(oi>=0){BrObject *o=&b->object[oi];if(br_attr(attrs,n,"src",v,sizeof v))br_resolve(b->base,v,o->url);br_attr(attrs,n,"alt",o->label,sizeof o->label);if(frame){br_attr(attrs,n,"name",o->name,sizeof o->name);if(!o->label[0])tw_copy(o->label,80,"Frame unavailable",17);}o->width=br_attr(attrs,n,"width",v,sizeof v)?br_dimension(v,0,2048):frame?(br_same(tag,"frame")?-100:300):0;o->height=br_attrnum(attrs,n,"height",frame?180:0,1024);if(br_same(tag,"frame")&&p->depth&&!br_has(attrs,n,"height"))o->ref=p->table[p->depth-1];}
        *pos=e+1;if(br_same(tag,"iframe"))br_content_end(s,e+1,"iframe",pos);return 1;
    }
    int area=br_same(tag,"textarea"),select=br_same(tag,"select"),button=br_same(tag,"button");
    if(br_same(tag,"input")||area||select||button){
        int after=e+1,end=e+1;if(area||select||button)end=br_content_end(s,e+1,tag,&after);*pos=after;
        if(b->controls==BR_CONTROLS){b->page.clipped=1;if(p->form>=0)b->form[p->form].unsupported|=2;return 1;}
        int ci=b->controls++;BrControl *c=&b->control[ci];c->form=p->form;c->object=-1;c->type=area?BC_AREA:select?BC_SELECT:button?BC_SUBMIT:BC_TEXT;
        if(br_attr(attrs,n,"type",v,sizeof v)){
            if(br_same(v,"password"))c->type=BC_PASSWORD;else if(br_same(v,"checkbox"))c->type=BC_CHECK;else if(br_same(v,"radio"))c->type=BC_RADIO;else if(br_same(v,"submit"))c->type=BC_SUBMIT;else if(br_same(v,"reset"))c->type=BC_RESET;else if(br_same(v,"hidden"))c->type=BC_HIDDEN;else if(br_same(v,"button"))c->type=BC_BUTTON;else if(br_same(v,"file"))c->type=BC_FILE;
        }
        c->disabled=(u8)(br_has(attrs,n,"disabled")||c->type==BC_FILE||c->type==BC_BUTTON);c->readonly=(u8)br_has(attrs,n,"readonly");c->checked=c->initial_checked=(u8)br_has(attrs,n,"checked");c->multiple=(u8)br_has(attrs,n,"multiple");c->choice=c->initial_choice=-1;c->option=b->options;c->count=0;c->limit=br_attrnum(attrs,n,"maxlength",255,255);if(!br_attr(attrs,n,"name",c->name,sizeof c->name)&&br_has(attrs,n,"name")&&c->form>=0)b->form[c->form].unsupported|=2;
        if(!br_attr(attrs,n,"value",c->value,sizeof c->value)){if(br_has(attrs,n,"value")&&c->form>=0)b->form[c->form].unsupported|=2;const char *v=c->type==BC_CHECK||c->type==BC_RADIO?"on":c->type==BC_SUBMIT?"Submit":c->type==BC_RESET?"Reset":"";tw_copy(c->value,sizeof c->value,v,tw_len(v));}
        if(area){br_decode(c->value,sizeof c->value,s+e+1,end-e-1);if(end-e-1>=256&&c->form>=0)b->form[c->form].unsupported|=2;}
        if(select){for(int q=e+1;q<end;){if(s[q]!='<'){q++;continue;}char name[12];int z,cl,at;if(!br_tag(s,q,&z,name,&cl,&at))break;q=z+1;if(cl||!br_same(name,"option"))continue;
                int stop=q;while(stop<end&&s[stop]!='<')stop++;if(b->options==BR_OPTIONS){b->page.clipped=1;if(c->form>=0)b->form[c->form].unsupported|=2;break;}BrOption *o=&b->option[b->options++];br_plain(o->label,sizeof o->label,s+q,stop-q);if(!br_attr(s+at,z-at,"value",o->value,sizeof o->value)){if((br_has(s+at,z-at,"value")||stop-q>=128)&&c->form>=0)b->form[c->form].unsupported|=2;br_plain(o->value,sizeof o->value,s+q,stop-q);}o->selected=o->initial=(u8)br_has(s+at,z-at,"selected");o->disabled=(u8)br_has(s+at,z-at,"disabled");if(o->selected)c->choice=c->count;c->count++;
            }if(c->choice<0&&!c->multiple)for(int k=0;k<c->count;k++)if(!b->option[c->option+k].disabled){c->choice=k;break;}c->initial_choice=c->choice;
        }
        if(c->type==BC_RADIO&&c->checked)for(int k=0;k<ci;k++){BrControl *old=&b->control[k];if(old->type==BC_RADIO&&old->form==c->form&&tw_eq(old->name,c->name))old->checked=old->initial_checked=0;}
        tw_copy(c->initial,sizeof c->initial,c->value,tw_len(c->value));
        if(c->type!=BC_HIDDEN){int oi=br_object(b,BR_CONTROL,st,0);c->object=oi;if(oi>=0){BrObject *o=&b->object[oi];o->ref=ci;o->width=br_attrnum(attrs,n,area?"cols":"size",area?28:20,80)*8+12;o->height=area?(br_attrnum(attrs,n,"rows",3,12)*16+8):24;if(o->height<24)o->height=24;
            if(c->type==BC_CHECK||c->type==BC_RADIO)o->width=20;
            if(c->type==BC_SUBMIT||c->type==BC_RESET||c->type==BC_BUTTON){if(button)br_plain(o->label,sizeof o->label,s+e+1,end-e-1);else tw_copy(o->label,sizeof o->label,c->value,tw_len(c->value)<80?tw_len(c->value):79);o->width=tw_len(o->label)*8+20;}
            if(c->type==BC_FILE){tw_copy(o->label,80,"File upload unavailable",23);o->width=204;}
            if(select&&c->multiple)o->height=(c->count<5?c->count:5)*20+4;
        }}return 1;
    }
    return 0;
}
static int br_encode(char *out,int cap,int *len,const char *s)
{
    for(int i=0;s[i];i++){u8 c=(u8)s[i];if(c=='\n'){if(*len+6>=cap)return 0;tw_copy(out+*len,cap-*len,"%0D%0A",6);*len+=6;continue;}
        if(c==' '){if(*len+1>=cap)return 0;out[(*len)++]='+';}else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='*'||c=='-'||c=='.'||c=='_'){if(*len+1>=cap)return 0;out[(*len)++]=(char)c;}else {if(*len+3>=cap)return 0;out[(*len)++]='%';out[(*len)++]="0123456789ABCDEF"[c>>4];out[(*len)++]="0123456789ABCDEF"[c&15];}}
    out[*len]=0;return 1;
}
static int br_pair(char *out,int cap,int *len,const char *name,const char *value)
{
    if(*len){if(*len+1>=cap)return 0;out[(*len)++]='&';}if(!br_encode(out,cap,len,name)||*len+1>=cap)return 0;out[(*len)++]='=';return br_encode(out,cap,len,value);
}
static int br_form_data(const BrPage *b,int form,int submit,char *out,int cap)
{
    int n=0;out[0]=0;if(form<0||form>=b->forms)return 0;
    for(int i=0;i<b->controls;i++){const BrControl *c=&b->control[i];if(c->form!=form||c->disabled||!c->name[0]||c->type==BC_RESET||c->type==BC_BUTTON||c->type==BC_FILE||(c->type==BC_SUBMIT&&i!=submit)||((c->type==BC_CHECK||c->type==BC_RADIO)&&!c->checked))continue;
        if(c->type==BC_SELECT){for(int k=0;k<c->count;k++){const BrOption *o=&b->option[c->option+k];if(!o->disabled&&(c->multiple?o->selected:c->choice==k)&&!br_pair(out,cap,&n,c->name,o->value))return 0;}}
        else if(!br_pair(out,cap,&n,c->name,c->value))return 0;
    }return 1;
}
static void br_form_reset(BrPage *b,int form)
{
    for(int i=0;i<b->controls;i++){BrControl *c=&b->control[i];if(c->form!=form)continue;tw_copy(c->value,sizeof c->value,c->initial,tw_len(c->initial));c->checked=c->initial_checked;c->choice=c->initial_choice;for(int k=0;k<c->count;k++)b->option[c->option+k].selected=b->option[c->option+k].initial;}
}
