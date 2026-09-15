#pragma once
#include "textweb_core.inc"
#define BR_RUNS 1024
#define BR_RULES 48
typedef struct {u8 fg,bg,flags,align;} BrStyle;
typedef struct {u16 start;u8 link,pad;BrStyle style;} BrRun;
typedef struct {TwPage page;BrRun run[BR_RUNS];int runs;u8 background;char title[80];} BrPage;
typedef struct {char selector[40];BrStyle style;u8 mask,weight;} BrRule;
typedef struct {BrRule rule[BR_RULES];int count;} BrCss;
typedef struct {char tag[12];BrStyle style;int link;} BrFrame;
static int br_space(int c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
static int br_same(const char *a,const char *b){return tw_len(a)==tw_len(b)&&tw_ci(a,b,tw_len(a));}
static int br_color(const char *s)
{
    static const char *const names[]={"black","navy","green","teal","maroon","purple","olive","silver","gray","blue","lime","aqua","red","fuchsia","yellow","white"};
    static const u8 rgb[16][3]={{0,0,0},{0,0,128},{0,128,0},{0,128,128},{128,0,0},{128,0,128},{128,128,0},{192,192,192},{128,128,128},{0,0,255},{0,255,0},{0,255,255},{255,0,0},{255,0,255},{255,255,0},{255,255,255}};
    for(int i=0;i<16;i++)if(br_same(s,names[i]))return i;
    if(br_same(s,"grey"))return 8;
    int n=tw_len(s),v[3];if(s[0]!='#'||(n!=4&&n!=7))return -1;
    for(int i=0;i<3;i++){int a=tw_hex(s[1+i*(n==4?1:2)]),b=n==4?a:tw_hex(s[2+i*2]);if(a<0||b<0)return -1;v[i]=a*16+b;}
    int best=0,dist=200000;for(int i=0;i<16;i++){int d=0;for(int j=0;j<3;j++){int x=v[j]-rgb[i][j];d+=x*x;}if(d<dist){dist=d;best=i;}}return best;
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
        else if((br_same(key,"background")||br_same(key,"background-color"))&&color>=0){style->bg=(u8)color;mask|=2;}
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
            for(int i=a;i<z;i++){int ch=s[i];if(ch=='#')weight+=100;else if(ch=='.')weight+=10;else if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='*'))valid=0;}
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
static void br_run(BrPage *b,BrStyle style,int link)
{
    if(b->runs){BrRun *r=&b->run[b->runs-1];if(r->style.fg==style.fg&&r->style.bg==style.bg&&r->style.flags==style.flags&&r->style.align==style.align&&r->link==link)return;
        if(r->start==b->page.len){r->style=style;r->link=(u8)link;return;}}
    if(b->runs==BR_RUNS){b->page.clipped=1;return;}
    BrRun *r=&b->run[b->runs++];r->start=(u16)b->page.len;r->style=style;r->link=(u8)link;r->pad=0;
}
static void br_put(BrPage *b,BrStyle style,int link,int c)
{
    if(style.flags&8)return;
    if(!(style.flags&4)&&br_space(c))c=' ';
    if(c==' '&&!(style.flags&4)&&(!b->page.len||br_space(b->page.text[b->page.len-1])))return;
    if(b->runs==BR_RUNS){b->page.clipped=1;return;}
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
    return br_same(s,"p")||br_same(s,"div")||br_same(s,"li")||br_same(s,"tr")||br_same(s,"ul")||br_same(s,"ol")||br_same(s,"pre")||br_same(s,"blockquote")||br_same(s,"center")||(s[0]=='h'&&s[1]>='1'&&s[1]<='6'&&!s[2]);
}
static int br_tag(const char *s,int p,int *end,char *tag,int *closing,int *attrs)
{
    int e=p+1,quote=0;while(s[e]){if(quote){if(s[e]==quote)quote=0;}else if(s[e]=='\''||s[e]=='"')quote=s[e];else if(s[e]=='>')break;e++;}
    if(!s[e])return 0;int a=p+1;*closing=s[a]=='/';if(*closing)a++;int z=a;while(z<e&&((s[z]>='a'&&s[z]<='z')||(s[z]>='A'&&s[z]<='Z')||(s[z]>='0'&&s[z]<='9')))z++;
    if(!tw_copy(tag,12,s+a,z-a))tag[0]=0;for(int i=0;tag[i];i++)if(tag[i]>='A'&&tag[i]<='Z')tag[i]+=32;
    *attrs=z;*end=e;return 1;
}
static void br_render(BrPage *b,const char *s,int mode,const char *base,BrCss *css)
{
    b->runs=0;b->background=15;b->title[0]=0;
    if(mode!=1){
        tw_render(&b->page,s,mode,base);BrStyle normal={0,15,0,0};br_run(b,normal,0);b->run[0].start=0;
        if(mode==2){int oldlen=b->page.len;for(int p=0;b->page.text[p];){int link=0;if(b->page.text[p]=='['){int q=p+1;while(link<=TW_LINKS&&b->page.text[q]>='0'&&b->page.text[q]<='9')link=link*10+b->page.text[q++]-'0';if(b->page.text[q]!=']'||link>b->page.links)link=0;}b->page.len=p;BrStyle st={link?1:0,15,link?2:0,0};br_run(b,st,link);while(b->page.text[p]&&b->page.text[p]!='\n')p++;if(b->page.text[p])p++;}b->page.len=oldlen;}
        return;
    }
    css->count=0;
    int script=0;
    for(int p=0;s[p];){if(s[p]!='<'){p++;continue;}
        if(!script&&s[p+1]=='!'&&s[p+2]=='-'&&s[p+3]=='-'){p+=4;while(s[p]&&!(s[p]=='-'&&s[p+1]=='-'&&s[p+2]=='>'))p++;if(s[p])p+=3;continue;}
        char tag[12];int e,close,a;if(!br_tag(s,p,&e,tag,&close,&a))break;
        if(br_same(tag,"script"))script=!close;
        if(!script&&br_same(tag,"style")&&!close){int z=e+1;while(s[z]&&!(s[z]=='<'&&s[z+1]=='/'&&tw_len(s+z+2)>=5&&tw_ci(s+z+2,"style",5)))z++;br_css(css,s+e+1,z-e-1);p=z;}else p=e+1;}
    b->page.len=b->page.links=b->page.clipped=0;b->page.text[0]=0;
    BrFrame stack[32];int depth=0,overflow=0,skip=0,title=0;BrFrame cur={{0},{0,15,0,0},0};
    for(int p=0;s[p];){
        if(s[p]=='<'){
            if(s[p+1]=='!'&&s[p+2]=='-'&&s[p+3]=='-'){p+=4;while(s[p]&&!(s[p]=='-'&&s[p+1]=='-'&&s[p+2]=='>'))p++;if(s[p])p+=3;continue;}
            char tag[12];int e,close,a;if(!br_tag(s,p,&e,tag,&close,&a))break;
            if(skip){if(close&&((skip==1&&br_same(tag,"script"))||(skip==2&&br_same(tag,"style"))))skip=0;p=e+1;continue;}
            if(br_same(tag,"script")||br_same(tag,"style")){if(!close)skip=br_same(tag,"script")?1:2;p=e+1;continue;}
            if(br_same(tag,"title")){title=!close;p=e+1;continue;}
            int block=br_block(tag);
            if(close){
                if(overflow)overflow--;else for(int i=depth-1;i>=0;i--)if(br_same(stack[i].tag,tag)){cur=stack[i];depth=i;break;}
                if(block)br_separate(b,tag);
            }else{
                if(!overflow&&(br_same(tag,"p")||br_same(tag,"li")||br_same(tag,"tr")||br_same(tag,"td")||br_same(tag,"th"))){
                    for(int i=depth-1;i>=0;i--){
                        const char *open=stack[i].tag;
                        if(br_same(open,tag)||((br_same(tag,"td")||br_same(tag,"th"))&&(br_same(open,"td")||br_same(open,"th")))){cur=stack[i];depth=i;break;}
                        if(br_same(open,"ul")||br_same(open,"ol")||br_same(open,"table"))break;
                    }
                }
                BrFrame next=cur;char value[256];
                if(block)br_separate(b,tag);
                if(br_same(tag,"head"))next.style.flags|=8;
                if(br_same(tag,"b")||br_same(tag,"strong")||(tag[0]=='h'&&tag[1]>='1'&&tag[1]<='6'&&!tag[2]))next.style.flags|=1;
                if(tag[0]=='h'&&tag[1]>='1'&&tag[1]<='6'&&!tag[2])next.style.fg=1;
                if(br_same(tag,"u"))next.style.flags|=2;if(br_same(tag,"pre"))next.style.flags|=4;if(br_same(tag,"center"))next.style.align=1;
                if(br_attr(s+a,e-a,"color",value,sizeof value)){int c=br_color(value);if(c>=0)next.style.fg=(u8)c;}
                if(br_attr(s+a,e-a,"bgcolor",value,sizeof value)){int c=br_color(value);if(c>=0)next.style.bg=(u8)c;}
                if(br_attr(s+a,e-a,"align",value,sizeof value)){if(br_same(value,"center"))next.style.align=1;else if(br_same(value,"right"))next.style.align=2;}
                if(br_same(tag,"a")&&br_attr(s+a,e-a,"href",value,sizeof value)){
                    char url[TW_URL];if(tw_resolve(base,value,url)){int len=b->page.len,link=tw_addlink(&b->page,url,value,tw_len(value));b->page.len=len;b->page.text[len]=0;next.link=link+1;if(next.link){next.style.fg=1;next.style.flags|=2;}}
                }
                br_style(&next.style,css,tag,s+a,e-a);
                if(br_same(tag,"body"))b->background=next.style.bg;
                int empty=br_same(tag,"br")||br_same(tag,"hr")||br_same(tag,"img")||br_same(tag,"meta")||br_same(tag,"link")||br_same(tag,"input")||br_same(tag,"base")||br_same(tag,"wbr")||(e>p&&s[e-1]=='/');
                if(br_same(tag,"br"))tw_put(&b->page,'\n',0);
                else if(br_same(tag,"hr")){br_break(b);for(int i=0;i<24;i++)br_put(b,next.style,0,'-');br_break(b);}
                else if(br_same(tag,"img")){if(br_attr(s+a,e-a,"alt",value,sizeof value))for(int i=0;value[i];i++)br_put(b,next.style,next.link,value[i]);}
                else if(br_same(tag,"li")){br_put(b,next.style,0,'*');br_put(b,next.style,0,' ');}
                else if(br_same(tag,"td")||br_same(tag,"th")){br_put(b,next.style,0,' ');br_put(b,next.style,0,'|');br_put(b,next.style,0,' ');}
                if(!empty&&tag[0]){if(depth<32&&!overflow){stack[depth]=cur;tw_copy(stack[depth].tag,sizeof stack[depth].tag,tag,tw_len(tag));depth++;cur=next;}else {overflow++;b->page.clipped=1;}}
            }
            p=e+1;continue;
        }
        int c=(u8)s[p++];if(c=='&'){int used=0,v=tw_entity(s+p,&used);if(v){c=v;p+=used;}}
        if(title){int n=tw_len(b->title);if(n<79){b->title[n]=(char)(br_space(c)?' ':c);b->title[n+1]=0;}continue;}
        if(skip)continue;br_put(b,cur.style,cur.link,c);
    }
    for(int i=0;i<b->runs;i++)if(b->run[i].link){TwLink *link=&b->page.link[b->run[i].link-1];int start=b->run[i].start,end=i+1<b->runs?b->run[i+1].start:b->page.len;if(end-start>79)end=start+79;if(end>start)tw_copy(link->label,sizeof link->label,b->page.text+start,end-start);}
}
static int br_run_at(const BrPage *b,int off){int lo=0,hi=b->runs;while(lo<hi){int mid=(lo+hi)/2;if(b->run[mid].start<=off)lo=mid+1;else hi=mid;}return lo?lo-1:0;}
