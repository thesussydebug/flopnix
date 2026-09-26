#pragma once
#include "textweb_core.inc"
#define BR_RUNS_MAX 65536
#define BR_RULES 96
#define BR_CSS_TEXT 32768
typedef struct {u8 fg,bg,flags,align;} BrStyle;
typedef struct {u32 start;u16 link,pad;BrStyle style;} BrRun;
#define BR_OBJECTS 64
#define BR_CONTROLS 32
#define BR_CELLS 96
#define BR_TABLES 12
#define BR_OPTIONS 64
#define BR_FORMS 8
#define BR_ANCHORS 32
enum { BR_IMAGE=1,BR_FRAME,BR_CONTROL,BR_TABLE,BR_RULE };
enum { BC_TEXT,BC_PASSWORD,BC_AREA,BC_CHECK,BC_RADIO,BC_SUBMIT,BC_RESET,BC_HIDDEN,BC_SELECT,BC_BUTTON,BC_FILE };
typedef struct {u32 start,end;u8 kind;u16 link;int width,height,ref;BrStyle style;char url[TW_URL],label[80],name[32];} BrObject;
typedef struct {char action[TW_URL],target[32];u8 post,unsupported;} BrForm;
typedef struct {char name[64],value[256],initial[256];int form,object,type,limit,option,count,choice,initial_choice;u8 checked,initial_checked,disabled,readonly,multiple;} BrControl;
typedef struct {char label[64],value[128];u8 selected,initial,disabled;} BrOption;
typedef struct {u32 start,end;int table,row,col,span,rows,width;BrStyle style;} BrCell;
typedef struct {int object,cols,rows,border,padding,spacing,width,frame,vertical;char sizes[80];u8 align;} BrTable;
typedef struct {char name[64];u32 off;} BrAnchor;
typedef struct {TwPage page;BrRun *run;int runs,run_cap;u8 background,link_fg;char title[80];
    BrObject object[BR_OBJECTS];BrControl control[BR_CONTROLS];BrForm form[BR_FORMS];BrOption option[BR_OPTIONS];BrCell cell[BR_CELLS];BrTable table[BR_TABLES];BrAnchor anchor[BR_ANCHORS];
    int objects,controls,forms,options,cells,tables,anchors;char base[TW_URL];
    char refresh[TW_URL];int refresh_delay;
} BrPage;
typedef struct {char selector[40];BrStyle style;u8 mask,weight;} BrRule;
typedef struct {BrRule rule[BR_RULES];int count;} BrCss;
typedef struct {char tag[12];BrStyle style;int link,count;} BrFrame;
static int br_space(int c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
static int br_same(const char *a,const char *b){return tw_len(a)==tw_len(b)&&tw_ci(a,b,tw_len(a));}
static const u8 br_rgb16[16][3]={{0,0,0},{0,0,128},{0,128,0},{0,128,128},{128,0,0},{128,0,128},{128,128,0},{192,192,192},{128,128,128},{0,0,255},{0,255,0},{0,255,255},{255,0,0},{255,0,255},{255,255,0},{255,255,255}};
static const char br_names[]="aliceblue antiquewhite aquamarine azure beige bisque blanchedalmond blueviolet brown burlywood cadetblue chartreuse chocolate coral cornflowerblue cornsilk crimson cyan darkblue darkcyan darkgoldenrod darkgray darkgreen darkgrey darkkhaki darkmagenta darkolivegreen darkorange darkorchid darkred darksalmon darkseagreen darkslateblue darkslategray darkslategrey darkturquoise darkviolet deeppink deepskyblue dimgray dimgrey dodgerblue firebrick floralwhite forestgreen gainsboro ghostwhite gold goldenrod greenyellow honeydew hotpink indianred indigo ivory khaki lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral lightcyan lightgoldenrodyellow lightgray lightgreen lightgrey lightpink lightsalmon lightseagreen lightskyblue lightslategray lightslategrey lightsteelblue lightyellow limegreen linen magenta mediumaquamarine mediumblue mediumorchid mediumpurple mediumseagreen mediumslateblue mediumspringgreen mediumturquoise mediumvioletred midnightblue mintcream mistyrose moccasin navajowhite oldlace olivedrab orange orangered orchid palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff peru pink plum powderblue rebeccapurple rosybrown royalblue saddlebrown salmon sandybrown seagreen seashell sienna skyblue slateblue slategray slategrey snow springgreen steelblue tan thistle tomato turquoise violet wheat whitesmoke yellowgreen";
static const u32 br_named[]={
    0xf0f8ff,0xfaebd7,0x7fffd4,0xf0ffff,0xf5f5dc,0xffe4c4,0xffebcd,0x8a2be2,0xa52a2a,0xdeb887,0x5f9ea0,0x7fff00,0xd2691e,0xff7f50,0x6495ed,0xfff8dc,
    0xdc143c,0x00ffff,0x00008b,0x008b8b,0xb8860b,0xa9a9a9,0x006400,0xa9a9a9,0xbdb76b,0x8b008b,0x556b2f,0xff8c00,0x9932cc,0x8b0000,0xe9967a,0x8fbc8f,
    0x483d8b,0x2f4f4f,0x2f4f4f,0x00ced1,0x9400d3,0xff1493,0x00bfff,0x696969,0x696969,0x1e90ff,0xb22222,0xfffaf0,0x228b22,0xdcdcdc,0xf8f8ff,0xffd700,
    0xdaa520,0xadff2f,0xf0fff0,0xff69b4,0xcd5c5c,0x4b0082,0xfffff0,0xf0e68c,0xe6e6fa,0xfff0f5,0x7cfc00,0xfffacd,0xadd8e6,0xf08080,0xe0ffff,0xfafad2,
    0xd3d3d3,0x90ee90,0xd3d3d3,0xffb6c1,0xffa07a,0x20b2aa,0x87cefa,0x778899,0x778899,0xb0c4de,0xffffe0,0x32cd32,0xfaf0e6,0xff00ff,0x66cdaa,0x0000cd,
    0xba55d3,0x9370db,0x3cb371,0x7b68ee,0x00fa9a,0x48d1cc,0xc71585,0x191970,0xf5fffa,0xffe4e1,0xffe4b5,0xffdead,0xfdf5e6,0x6b8e23,0xffa500,0xff4500,
    0xda70d6,0xeee8aa,0x98fb98,0xafeeee,0xdb7093,0xffefd5,0xffdab9,0xcd853f,0xffc0cb,0xdda0dd,0xb0e0e6,0x663399,0xbc8f8f,0x4169e1,0x8b4513,0xfa8072,
    0xf4a460,0x2e8b57,0xfff5ee,0xa0522d,0x87ceeb,0x6a5acd,0x708090,0x708090,0xfffafa,0x00ff7f,0x4682b4,0xd2b48c,0xd8bfd8,0xff6347,0x40e0d0,0xee82ee,
    0xf5deb3,0xf5f5f5,0x9acd32};
static int br_nearest(int r,int g,int b)
{
    int best=0,dist=0x7fffffff;for(int i=0;i<16;i++){int x=r-br_rgb16[i][0],y=g-br_rgb16[i][1],z=b-br_rgb16[i][2],d=x*x+y*y+z*z;if(d<dist){dist=d;best=i;}}return best;
}
static int br_luma(int c){const u8 *v=br_rgb16[c&15];return (v[0]*77+v[1]*150+v[2]*29)>>8;}
static int br_contrast(int fg,int bg){int d=br_luma(fg)-br_luma(bg);if(d<0)d=-d;return d<32?(br_luma(bg)<128?15:0):fg;}
static int br_color(const char *s)
{
    static const char *const names[]={"black","navy","green","teal","maroon","purple","olive","silver","gray","blue","lime","aqua","red","fuchsia","yellow","white"};
    for(int i=0;i<16;i++)if(br_same(s,names[i]))return i;
    if(br_same(s,"grey"))return 8;
    int n=tw_len(s),v[3];
    for(int i=0,a=0;br_names[a];i++){int z=a;while(br_names[z]&&br_names[z]!=' ')z++;if(z-a==n&&tw_ci(s,br_names+a,n)){u32 c=br_named[i];return br_nearest((int)(c>>16),(int)((c>>8)&255),(int)(c&255));}a=br_names[z]?z+1:z;}
    if(n>=10&&tw_ci(s,"rgb",3)){
        int p=3;if(s[p]=='a'||s[p]=='A')p++;while(br_space(s[p]))p++;if(s[p++]!='(')return -1;
        for(int k=0;k<3;k++){while(br_space(s[p]))p++;if(s[p]<'0'||s[p]>'9')return -1;int x=0;while(s[p]>='0'&&s[p]<='9'){if(x<1000)x=x*10+s[p]-'0';p++;}
            if(s[p]=='.'){p++;while(s[p]>='0'&&s[p]<='9')p++;}if(s[p]=='%'){p++;x=x>100?255:x*255/100;}v[k]=x>255?255:x;while(br_space(s[p]))p++;if(k<2&&s[p++]!=',')return -1;}
        return s[p]==')'||s[p]==','||s[p]=='/'?br_nearest(v[0],v[1],v[2]):-1;
    }
    const char *h=s;int m=n;if(h[0]=='#'){h++;m--;}else if(m!=6)return -1;
    if(m!=3&&m!=6)return -1;
    for(int i=0;i<3;i++){int a=tw_hex(h[i*(m==3?1:2)]),b=m==3?a:tw_hex(h[1+i*2]);if(a<0||b<0)return -1;v[i]=a*16+b;}
    return br_nearest(v[0],v[1],v[2]);
}
static void br_apply(BrStyle *d,BrStyle s,int mask)
{
    if(mask&1)d->fg=s.fg;if(mask&2)d->bg=s.bg;if(mask&4)d->flags=(d->flags&~1)|(s.flags&1);
    if(mask&8)d->flags=(d->flags&~2)|(s.flags&2);if(mask&16)d->align=s.align;
    if(mask&32)d->flags=(d->flags&~4)|(s.flags&4);if(mask&64)d->flags=(d->flags&~8)|(s.flags&8);
}
static int br_declarations(const char *s,int len,BrStyle *style)
{
    int mask=0;
    for(int p=0;p<len;){
        while(p<len&&(br_space(s[p])||s[p]==';'))p++;
        int a=p;while(p<len&&s[p]!=':'&&s[p]!=';')p++;if(p==len)break;if(s[p]!=':'){p++;continue;}
        int b=p++;while(b>a&&br_space(s[b-1]))b--;while(p<len&&br_space(s[p]))p++;
        int c=p;while(p<len&&s[p]!=';')p++;int e=p;while(e>c&&br_space(s[e-1]))e--;
        char key[40],value[80];if(!tw_copy(key,sizeof key,s+a,b-a)||!tw_copy(value,sizeof value,s+c,e-c))continue;
        int color=br_color(value);
        if(br_same(key,"color")&&color>=0){style->fg=(u8)color;mask|=1;}
        else if(br_same(key,"background")||br_same(key,"background-color")){
            for(int t=0;color<0&&value[t];){while(value[t]==' ')t++;int z=t;while(value[z]&&value[z]!=' ')z++;char token[40];if(z>t&&tw_copy(token,sizeof token,value+t,z-t))color=br_color(token);t=z;}
            if(color<0)continue;style->bg=(u8)color;mask|=2;}
        else if(br_same(key,"font-weight")){if(br_same(value,"bold")||br_same(value,"bolder")||(value[0]>='6'&&value[0]<='9'&&tw_len(value)==3))style->flags|=1;else if(br_same(value,"normal")||br_same(value,"400"))style->flags&=~1;else continue;mask|=4;}
        else if(br_same(key,"text-decoration")){if(br_same(value,"underline"))style->flags|=2;else if(br_same(value,"none"))style->flags&=~2;else continue;mask|=8;}
        else if(br_same(key,"text-align")){if(br_same(value,"left"))style->align=0;else if(br_same(value,"center"))style->align=1;else if(br_same(value,"right"))style->align=2;else continue;mask|=16;}
        else if(br_same(key,"white-space")){if(br_same(value,"pre"))style->flags|=4;else if(br_same(value,"normal"))style->flags&=~4;else continue;mask|=32;}
        else if(br_same(key,"display")&&br_same(value,"none")){style->flags|=8;mask|=64;}
    }
    return mask;
}
static void br_css(BrCss *css,const char *s,int len)
{
    int p=0;
    while(p<len&&css->count<BR_RULES){
        if(p+1<len&&s[p]=='/'&&s[p+1]=='*'){p+=2;while(p+1<len&&!(s[p]=='*'&&s[p+1]=='/'))p++;p+=2;continue;}
        if(br_space(s[p])){p++;continue;}int a=p;while(p<len&&s[p]!='{')p++;if(p==len)break;int b=p++;
        int c=p;while(p<len&&s[p]!='}')p++;BrStyle style={0};int mask=br_declarations(s+c,p-c,&style);if(p<len)p++;
        while(a<b&&css->count<BR_RULES){
            while(a<b&&br_space(s[a]))a++;int e=a;while(e<b&&s[e]!=',')e++;int z=e;while(z>a&&br_space(s[z-1]))z--;
            BrRule *r=&css->rule[css->count];int valid=z>a,weight=0;
            for(int i=a;i<z;i++){int ch=s[i];if(ch=='#')weight+=100;else if(ch=='.')weight+=10;
                else if(ch==':'&&((z-i==5&&tw_ci(s+i+1,"link",4))||(z-i==8&&tw_ci(s+i+1,"visited",7)))){z=i;break;}
                else if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='*'))valid=0;}
            if(valid&&mask&&tw_copy(r->selector,sizeof r->selector,s+a,z-a)){r->style=style;r->mask=(u8)mask;r->weight=(u8)(weight+(s[a]!='.'&&s[a]!='#'&&s[a]!='*'));css->count++;}a=e+1;
        }
    }
}
static void br_decode(char *out,int cap,const char *s,int n)
{
    int o=0;for(int i=0;i<n&&o<cap-1;i++){
        int c=(u8)s[i];if(c=='&'){char entity[12];int k=0;while(i+1+k<n&&k<11){entity[k]=s[i+1+k];k++;if(entity[k-1]==';')break;}entity[k]=0;int used=0,v=tw_entity(entity,&used);if(v){c=v;i+=used;}}
        if(c>=32||c=='\t'||c=='\n')out[o++]=(char)c;
    }out[o]=0;
}
static int br_attr(const char *s,int n,const char *key,char *out,int cap)
{
    out[0]=0;
    for(int p=0;p<n;){
        while(p<n&&(br_space(s[p])||s[p]=='/'))p++;int a=p;while(p<n&&!br_space(s[p])&&s[p]!='='&&s[p]!='/')p++;int b=p;
        while(p<n&&br_space(s[p]))p++;if(p==a){p++;continue;}if(p==n||s[p]!='=')continue;
        p++;while(p<n&&br_space(s[p]))p++;int quote=p<n&&(s[p]=='\''||s[p]=='"')?s[p++]:0,c=p;
        while(p<n&&(quote?s[p]!=quote:!br_space(s[p])))p++;int e=p;if(quote&&p<n)p++;
        if(b-a==tw_len(key)&&tw_ci(s+a,key,b-a)){if(e-c>=cap)return 0;br_decode(out,cap,s+c,e-c);return 1;}
    }return 0;
}
static int br_word(const char *list,const char *s,int n)
{
    while(*list){while(br_space(*list))list++;const char *a=list;while(*list&&!br_space(*list))list++;if(list-a==n){int i=0;while(i<n&&a[i]==s[i])i++;if(i==n)return 1;}}return 0;
}
static int br_match(const char *sel,const char *tag,const char *id,const char *classes)
{
    int p=0;while(sel[p]&&sel[p]!='.'&&sel[p]!='#')p++;
    if(p&&!(p==1&&sel[0]=='*')&&(p!=tw_len(tag)||!tw_ci(sel,tag,p)))return 0;
    while(sel[p]){int kind=sel[p++],a=p;while(sel[p]&&sel[p]!='.'&&sel[p]!='#')p++;int n=p-a;if(!n)return 0;
        if(kind=='.'){if(!br_word(classes,sel+a,n))return 0;}else {if(tw_len(id)!=n)return 0;for(int i=0;i<n;i++)if(id[i]!=sel[a+i])return 0;}}
    return 1;
}
static void br_style(BrStyle *style,const BrCss *css,const char *tag,const char *attrs,int n)
{
    char id[80],classes[128],value[256];br_attr(attrs,n,"id",id,sizeof id);br_attr(attrs,n,"class",classes,sizeof classes);
    int weights[7]={-1,-1,-1,-1,-1,-1,-1};
    for(int i=0;i<css->count;i++){const BrRule *r=&css->rule[i];if(!br_match(r->selector,tag,id,classes))continue;
        int mask=0;for(int bit=0;bit<7;bit++)if((r->mask&(1<<bit))&&r->weight>=weights[bit]){weights[bit]=r->weight;mask|=1<<bit;}br_apply(style,r->style,mask);}
    if(br_attr(attrs,n,"style",value,sizeof value)){BrStyle s={0};int mask=br_declarations(value,tw_len(value),&s);br_apply(style,s,mask);}
}
static int br_run_room(BrPage *b)
{
    if(b->runs<b->run_cap)return 1;if(b->run_cap>=BR_RUNS_MAX||!tw_grow)return 0;
    int cap=b->run_cap?b->run_cap*2:256;BrRun *r=tw_grow(b->run,(u32)cap*sizeof *r);if(!r)return 0;
    b->run=r;b->run_cap=cap;return 1;
}
static void br_page_free(BrPage *b){tw_free(&b->page);if(b->run_cap)tw_grow(b->run,0);b->run=0;b->runs=b->run_cap=0;}
static void br_run(BrPage *b,BrStyle style,int link)
{
    if(b->runs){BrRun *r=&b->run[b->runs-1];if(r->style.fg==style.fg&&r->style.bg==style.bg&&r->style.flags==style.flags&&r->style.align==style.align&&r->link==link)return;
        if(r->start==(u32)b->page.len){r->style=style;r->link=(u16)link;return;}}
    if(!br_run_room(b)){b->page.clipped=1;return;}
    BrRun *r=&b->run[b->runs++];r->start=(u32)b->page.len;r->style=style;r->link=(u16)link;r->pad=0;
}
static void br_put(BrPage *b,BrStyle style,int link,int c)
{
    if(style.flags&8)return;
    if(!(style.flags&4)&&br_space(c))c=' ';
    if(c==' '&&!(style.flags&4)&&(!b->page.len||br_space(b->page.text[b->page.len-1])))return;
    if(!br_run_room(b)){b->page.clipped=1;return;}
    br_run(b,style,link);tw_put(&b->page,c,0);
}
static void br_break(BrPage *b){if(b->page.len&&b->page.text[b->page.len-1]!='\n')tw_put(&b->page,'\n',0);}
static void br_separate(BrPage *b,const char *tag)
{
    br_break(b);
    if(b->page.len>1&&(br_same(tag,"p")||br_same(tag,"ul")||br_same(tag,"ol")||(tag[0]=='h'&&tag[1]>='1'&&tag[1]<='6'&&!tag[2]))&&b->page.text[b->page.len-2]!='\n')tw_put(&b->page,'\n',0);
}
static int br_block(const char *s)
{
    return br_same(s,"p")||br_same(s,"div")||br_same(s,"li")||br_same(s,"tr")||br_same(s,"ul")||br_same(s,"ol")||br_same(s,"pre")||br_same(s,"blockquote")||br_same(s,"center")||(s[0]=='h'&&s[1]>='1'&&s[1]<='6'&&!s[2])||
        br_same(s,"dl")||br_same(s,"dt")||br_same(s,"dd")||br_same(s,"dir")||br_same(s,"menu")||br_same(s,"address");
}
static int br_list(const char *s){return br_same(s,"ul")||br_same(s,"ol")||br_same(s,"dir")||br_same(s,"menu");}
static void br_text(BrPage *b,BrStyle st,int link,const char *s){while(*s)br_put(b,st,link,(u8)*s++);}
static int br_tag(const char *s,int p,int *end,char *tag,int *closing,int *attrs)
{
    int e=p+1,quote=0;while(s[e]){if(quote){if(s[e]==quote)quote=0;}else if(s[e]=='\''||s[e]=='"')quote=s[e];else if(s[e]=='>')break;e++;}
    if(!s[e])return 0;int a=p+1;*closing=s[a]=='/';if(*closing)a++;int z=a;while(z<e&&((s[z]>='a'&&s[z]<='z')||(s[z]>='A'&&s[z]<='Z')||(s[z]>='0'&&s[z]<='9')))z++;
    if(!tw_copy(tag,12,s+a,z-a))tag[0]=0;for(int i=0;tag[i];i++)if(tag[i]>='A'&&tag[i]<='Z')tag[i]+=32;
    *attrs=z;*end=e;return 1;
}
#include "browser_elements.inc"
static int br_refresh(BrPage *b,const char *s)
{
    int p=0,delay=0;while(br_space(s[p]))p++;if(s[p]<'0'||s[p]>'9')return 0;
    while(s[p]>='0'&&s[p]<='9'){if(delay<100000)delay=delay*10+s[p]-'0';p++;}
    while((s[p]>='0'&&s[p]<='9')||s[p]=='.')p++;
    while(br_space(s[p]))p++;if(s[p]==';'||s[p]==',')p++;while(br_space(s[p]))p++;
    if(tw_ci(s+p,"url",3)){int q=p+3;while(br_space(s[q]))q++;if(s[q]=='='){p=q+1;while(br_space(s[p]))p++;}}
    int quote=s[p]=='\''||s[p]=='"'?s[p++]:0,e=p;while(s[e]&&s[e]!=quote)e++;while(e>p&&br_space(s[e-1]))e--;
    char raw[TW_URL],url[TW_URL];if(e==p||!tw_copy(raw,sizeof raw,s+p,e-p))return 0;
    if(!br_resolve(b->base,raw,url))tw_copy(url,TW_URL,raw,e-p);
    if(tw_eq(url,b->base))return 0;
    tw_copy(b->refresh,TW_URL,url,tw_len(url));b->refresh_delay=delay;
    BrStyle st={0,15,0,0};char label[32];int n=0;
    if(delay>1){const char *pre="Refresh after ";while(*pre)label[n++]=*pre++;char digits[6];int k=0;do{digits[k++]=(char)('0'+delay%10);delay/=10;}while(delay&&k<6);while(k)label[n++]=digits[--k];label[n++]=' ';label[n++]='s';}
    else {const char *pre="Refresh";while(*pre)label[n++]=*pre++;}
    label[n++]=':';label[n++]=' ';label[n]=0;br_break(b);br_text(b,st,0,label);
    int len=b->page.len,link=tw_addlink(&b->page,url,url,tw_len(url));b->page.len=len;b->page.text[len]=0;
    if(link>=0){BrStyle ls={b->link_fg,15,2,0};br_text(b,ls,link+1,url);}else br_text(b,st,0,url);
    br_break(b);tw_put(&b->page,'\n',0);return b->page.len;
}
static void br_render_extra(BrPage *b,const char *s,int mode,const char *base,BrCss *css,const char *external)
{
    b->runs=0;b->background=15;b->link_fg=1;b->title[0]=0;b->refresh[0]=0;b->refresh_delay=-1;
    b->objects=b->controls=b->forms=b->options=b->cells=b->tables=b->anchors=0;tw_copy(b->base,TW_URL,base,tw_len(base));
    if(mode!=1){
        tw_render(&b->page,s,mode,base);BrStyle normal={0,15,0,0};br_run(b,normal,0);if(b->runs)b->run[0].start=0;
        if(mode==2){int oldlen=b->page.len;for(int p=0;b->page.text[p];){int link=0;if(b->page.text[p]=='['){int q=p+1;while(link<=TW_LINKS_MAX&&b->page.text[q]>='0'&&b->page.text[q]<='9')link=link*10+b->page.text[q++]-'0';if(b->page.text[q]!=']'||link>b->page.links)link=0;}b->page.len=p;BrStyle st={link?1:0,15,link?2:0,0};br_run(b,st,link);while(b->page.text[p]&&b->page.text[p]!='\n')p++;if(b->page.text[p])p++;}b->page.len=oldlen;}
        return;
    }
    css->count=0;if(external)br_css(css,external,tw_len(external));
    int script=0;
    for(int p=0;s[p];){if(s[p]!='<'){p++;continue;}
        if(!script&&s[p+1]=='!'&&s[p+2]=='-'&&s[p+3]=='-'){p+=4;while(s[p]&&!(s[p]=='-'&&s[p+1]=='-'&&s[p+2]=='>'))p++;if(s[p])p+=3;continue;}
        char tag[12];int e,close,a;if(!br_tag(s,p,&e,tag,&close,&a))break;
        if(br_same(tag,"script"))script=!close;
        if(!script&&br_same(tag,"style")&&!close){int z=e+1;while(s[z]&&!(s[z]=='<'&&s[z+1]=='/'&&tw_len(s+z+2)>=5&&tw_ci(s+z+2,"style",5)))z++;br_css(css,s+e+1,z-e-1);p=z;}else p=e+1;}
    tw_clear(&b->page);
    BrParse state; br_parse_init(&state);
    BrFrame stack[32];int depth=0,overflow=0,skip=0,title=0,refresh_start=0,refresh_end=0;BrFrame cur={{0},{0,15,0,0},0,0};
    for(int p=0;s[p];){
        if(s[p]=='<'){
            if(s[p+1]=='!'&&s[p+2]=='-'&&s[p+3]=='-'){p+=4;while(s[p]&&!(s[p]=='-'&&s[p+1]=='-'&&s[p+2]=='>'))p++;if(s[p])p+=3;continue;}
            char tag[12];int e,close,a;if(!br_tag(s,p,&e,tag,&close,&a))break;
            if(skip){if(close&&((skip==1&&br_same(tag,"script"))||(skip==2&&br_same(tag,"style"))))skip=0;p=e+1;continue;}
            if(br_same(tag,"script")||br_same(tag,"style")){if(!close)skip=br_same(tag,"script")?1:2;p=e+1;continue;}
            if(br_same(tag,"title")){title=!close;p=e+1;continue;}
            int block=br_block(tag);
            if(close){
                br_close_element(b,&state,tag);
                if(overflow)overflow--;else for(int i=depth-1;i>=0;i--)if(br_same(stack[i].tag,tag)){cur=stack[i];depth=i;break;}
                if(block){if(br_list(tag)&&((cur.style.flags>>4)&7))br_break(b);else br_separate(b,tag);}
            }else{
                if(!overflow&&(br_same(tag,"p")||br_same(tag,"li")||br_same(tag,"tr")||br_same(tag,"td")||br_same(tag,"th"))){
                    for(int i=depth-1;i>=0;i--){
                        const char *open=stack[i].tag;
                        if(br_same(open,tag)||((br_same(tag,"td")||br_same(tag,"th"))&&(br_same(open,"td")||br_same(open,"th")))){cur=stack[i];depth=i;break;}
                        if(br_same(open,"ul")||br_same(open,"ol")||br_same(open,"table"))break;
                    }
                }
                BrFrame next=cur;char value[256];
                if(block){if(br_list(tag)&&((cur.style.flags>>4)&7))br_break(b);else br_separate(b,tag);}
                if(br_same(tag,"head")||br_same(tag,"noframes"))next.style.flags|=8;
                if(br_same(tag,"b")||br_same(tag,"strong")||br_same(tag,"th")||(tag[0]=='h'&&tag[1]>='1'&&tag[1]<='6'&&!tag[2]))next.style.flags|=1;
                if(tag[0]=='h'&&tag[1]>='1'&&tag[1]<='6'&&!tag[2])next.style.fg=1;
                if(br_same(tag,"u"))next.style.flags|=2;if(br_same(tag,"pre"))next.style.flags|=4;if(br_same(tag,"center"))next.style.align=1;
                if(br_list(tag)||br_same(tag,"blockquote")||br_same(tag,"dd")){int level=(next.style.flags>>4)&7;if(level<7)next.style.flags=(u8)((next.style.flags&~0x70)|((level+1)<<4));}
                if((br_same(tag,"font")||br_same(tag,"hr"))&&br_attr(s+a,e-a,"color",value,sizeof value)){int c=br_color(value);if(c>=0)next.style.fg=(u8)c;}
                if(br_attr(s+a,e-a,"bgcolor",value,sizeof value)){int c=br_color(value);if(c>=0)next.style.bg=(u8)c;}
                if(br_attr(s+a,e-a,"align",value,sizeof value)){if(br_same(value,"center"))next.style.align=1;else if(br_same(value,"right"))next.style.align=2;}
                if(br_same(tag,"a")&&br_attr(s+a,e-a,"href",value,sizeof value)){
                    char url[TW_URL];if(br_resolve(b->base,value,url)){int len=b->page.len,link=tw_addlink(&b->page,url,value,tw_len(value));b->page.len=len;b->page.text[len]=0;next.link=link+1;if(next.link){br_attr(s+a,e-a,"target",b->page.link[link].target,32);b->page.link[link].download=(u8)br_has(s+a,e-a,"download");next.style.fg=b->link_fg;next.style.flags|=2;}}
                }
                br_style(&next.style,css,tag,s+a,e-a);
                if(br_same(tag,"body")){if(br_attr(s+a,e-a,"text",value,sizeof value)){int c=br_color(value);if(c>=0)next.style.fg=(u8)c;}if(br_attr(s+a,e-a,"link",value,sizeof value)){int c=br_color(value);if(c>=0)b->link_fg=(u8)c;}b->background=next.style.bg;}
                br_anchor(b,s+a,e-a,tag);
                if(br_same(tag,"meta")&&!b->refresh[0]&&br_attr(s+a,e-a,"http-equiv",value,sizeof value)&&br_same(value,"refresh")&&br_attr(s+a,e-a,"content",value,sizeof value)){int start=b->page.len,end=br_refresh(b,value);if(end){refresh_start=start;refresh_end=end;}}
                if(br_open_element(b,&state,s,&p,e,a,tag,next.style,next.link)){continue;}
                if(br_same(tag,"table"))next.style.align=cur.style.align;
                int empty=br_same(tag,"br")||br_same(tag,"hr")||br_same(tag,"img")||br_same(tag,"meta")||br_same(tag,"link")||br_same(tag,"input")||br_same(tag,"base")||br_same(tag,"wbr")||(e>p&&s[e-1]=='/');
                if(br_same(tag,"br"))tw_put(&b->page,'\n',0);
                else if(br_same(tag,"hr")){br_break(b);for(int i=0;i<24;i++)br_put(b,next.style,0,'-');br_break(b);}
                else if(br_same(tag,"img")){if(br_attr(s+a,e-a,"alt",value,sizeof value))for(int i=0;value[i];i++)br_put(b,next.style,next.link,value[i]);}
                else if(br_same(tag,"li")){int num=0;for(int i=depth-1;i>=0;i--)if(br_list(stack[i].tag)){if(br_same(stack[i].tag,"ol"))num=++stack[i].count;break;}
                    if(num>0){char d[8];int k=0;do{d[k++]=(char)('0'+num%10);num/=10;}while(num&&k<7);while(k)br_put(b,next.style,0,d[--k]);br_put(b,next.style,0,'.');}else br_put(b,next.style,0,'*');br_put(b,next.style,0,' ');}

                if(!empty&&tag[0]){if(depth<32&&!overflow){stack[depth]=cur;tw_copy(stack[depth].tag,sizeof stack[depth].tag,tag,tw_len(tag));stack[depth].count=br_same(tag,"ol")?br_attrnum(s+a,e-a,"start",1,30000)-1:0;depth++;cur=next;}else {overflow++;b->page.clipped=1;}}
            }
            p=e+1;continue;
        }
        int c=(u8)s[p++];if(c=='&'){int used=0,v=tw_entity(s+p,&used);if(v){c=v;p+=used;}}
        if(title){int n=tw_len(b->title);if(n<79){b->title[n]=(char)(br_space(c)?' ':c);b->title[n+1]=0;}continue;}
        if(skip)continue;br_put(b,cur.style,cur.link,c);
    }
    br_finish_elements(b,&state);
    for(int i=0;i<b->runs;i++)if(b->run[i].start>=(u32)refresh_start&&b->run[i].start<(u32)refresh_end){b->run[i].style.bg=b->background;if(b->run[i].link)b->run[i].style.fg=b->link_fg;}
    for(int i=0;i<b->runs;i++)if(b->run[i].link){TwLink *link=&b->page.link[b->run[i].link-1];int start=(int)b->run[i].start,end=i+1<b->runs?(int)b->run[i+1].start:b->page.len;if(end-start>79)end=start+79;if(end>start)tw_copy(link->label,sizeof link->label,b->page.text+start,end-start);}
}
static int br_run_at(const BrPage *b,int off){int lo=0,hi=b->runs;while(lo<hi){int mid=(lo+hi)/2;if((int)b->run[mid].start<=off)lo=mid+1;else hi=mid;}return lo?lo-1:0;}

static void br_render(BrPage *b,const char *s,int mode,const char *base,BrCss *css){br_render_extra(b,s,mode,base,css,0);}
