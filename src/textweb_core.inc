/* Parses text-browser addresses and responses. */
#pragma once
#include "shcwd.inc"
#define TW_URL 256
#define TW_TEXT_MAX 262144
#define TW_LINKS_MAX 1024
typedef struct { char host[80], path[176]; u16 port; u8 http, type; } TwUrl;
typedef struct { char url[TW_URL], label[80], target[32]; u8 download; } TwLink;
typedef struct { char *text; TwLink *link; int len, links, clipped, text_cap, link_cap; } TwPage;
static void *(*tw_grow)(void *p,u32 size);
static char tw_blank[1];
static inline int tw_eq(const char *a,const char *b) { while (*a && *a==*b) {a++;b++;} return *a==*b; }
static int tw_contains(const char *a,const char *b) {
    for (;*a;a++) {int i=0;while(b[i]&&a[i]==b[i])i++;if(!b[i])return 1;}return 0;
}
static int tw_len(const char *s) { int n=0;while(s[n])n++;return n; }
static int tw_copy(char *d,int cap,const char *s,int n) {
    if(n<0||n>=cap)return 0;for(int i=0;i<n;i++)d[i]=s[i];d[n]=0;return 1;
}
static int tw_hex(int c) {
    if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;
}
static int tw_ci(const char *a,const char *b,int n) {
    for(int i=0;i<n;i++){int c=a[i];if(c>='A'&&c<='Z')c+=32;if(c!=b[i])return 0;}return 1;
}

static int tw_url(const char *s,TwUrl *u)
{
    int n=tw_len(s), p;
    if(n>=TW_URL)return 0;
    if(n>=7&&tw_ci(s,"http://",7)){u->http=1;u->port=80;u->type='h';p=7;}
    else if(n>=9&&tw_ci(s,"gopher://",9)){u->http=0;u->port=70;u->type='1';p=9;}
    else return 0;
    int begin=p;
    while(s[p]&&s[p]!='/'&&s[p]!=':'&&s[p]!='?'&&s[p]!='#') {
        int c=s[p];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return 0;p++;
    }
    if(p==begin||!tw_copy(u->host,sizeof u->host,s+begin,p-begin))return 0;
    if(s[p]==':'){
        u32 port=0;int digs=0;p++;
        while(s[p]>='0'&&s[p]<='9'){port=port*10+s[p++]-'0';if(port>65535)return 0;digs++;}
        if(!digs||!port)return 0;u->port=(u16)port;
    }
    if(s[p]&&s[p]!='/'&&s[p]!='?'&&s[p]!='#')return 0;
    int o=0,tabs=0;
    if(!u->http&&s[p]=='/'){p++;if(s[p]&&s[p]!='#')u->type=(u8)s[p++];}
    if(!u->http&&u->type!='0'&&u->type!='1'&&u->type!='7'&&u->type!='h'&&u->type!='9'&&u->type!='5'&&u->type!='4'&&u->type!='6'&&u->type!='g'&&u->type!='I'&&u->type!='s')return 0;
    if(u->http&&s[p]!='/')u->path[o++]='/';
    for(;s[p]&&s[p]!='#';p++){
        int c=(u8)s[p];
        if(c<=32||c==127)return 0;
        if(!u->http&&c=='%'){
            if(!s[p+1]||!s[p+2])return 0;
            int a=tw_hex(s[p+1]),b=tw_hex(s[p+2]);if(a<0||b<0)return 0;c=a*16+b;p+=2;
            if(c==9&&u->type=='7'&&!tabs)tabs++;
            else if(c<32||c==127)return 0;
        }
        if(o>= (int)sizeof u->path-1)return 0;u->path[o++]=(char)c;
    }
    u->path[o]=0;return 1;
}
static int tw_encoded(char *out,int cap,const char *s)
{
    int n=0;
    for(;*s;s++){
        u8 c=(u8)*s;
        if(c<33||c>=127||c=='%'||c=='#'||c=='?'){
            if(n+3>=cap)return 0;out[n++]='%';out[n++]="0123456789ABCDEF"[c>>4];out[n++]="0123456789ABCDEF"[c&15];
        }else{if(n+1>=cap)return 0;out[n++]=(char)c;}
    }
    out[n]=0;return 1;
}

static int tw_local(const char *s,char *out)
{
    if(!s[0]||s[1]!=':'||!(s[0]=='a'||s[0]=='A'||s[0]=='u'||s[0]=='U'))return 0;
    int usb=s[0]=='u'||s[0]=='U',n=tw_len(s);
    if(n<3||n>=TW_URL||s[n-1]=='/'||s[n-1]=='\\')return 0;
    for(int i=2;i<n;i++)if((u8)s[i]<32||s[i]==':'||s[i]=='"')return 0;
    char path[128];if(!sc_resolve("",s+2,path,sizeof path)||!path[0]||(!usb&&tw_len(path)>=FS_NAMELEN))return 0;
    out[0]=usb?'u':'a';out[1]=':';if(usb)out[2]='/';
    return tw_copy(out+2+usb,TW_URL-2-usb,path,tw_len(path));
}
// finds "key url" in the search list, # lines are ignored
static const char *tw_engine(const char *cfg,const char *key,int kn,int *un)
{
    for(const char *l=cfg;l&&*l;){
        const char *e=l;while(*e&&*e!='\n')e++;
        const char *w=l;while(w<e&&(*w==' '||*w=='\t'))w++;
        const char *p=w;while(p<e&&*p!=' '&&*p!='\t')p++;
        const char *u=p;while(u<e&&(*u==' '||*u=='\t'))u++;
        const char *ue=e;while(ue>u&&(ue[-1]=='\r'||ue[-1]==' '||ue[-1]=='\t'))ue--;
        int same=p-w==kn&&*w!='#'&&ue>u;
        for(int i=0;same&&i<kn;i++){int a=w[i],b=key[i];if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;same=a==b;}
        if(same){*un=(int)(ue-u);return u;}
        l=*e?e+1:e;
    }
    return 0;
}
static int tw_search_url(const char *typed,const char *cfg,char *out)
{
    while(*typed==' ')typed++;
    int n=tw_len(typed);while(n&&typed[n-1]==' ')n--;
    char t[TW_URL],local[TW_URL];TwUrl u;
    if(!n||!tw_copy(t,TW_URL,typed,n)||tw_url(t,&u)||tw_local(t,local))return 0;
    int space=0,dot=0,colon=0;for(int i=0;i<n;i++){space|=t[i]==' ';dot|=t[i]=='.';colon|=t[i]==':';}
    if(!space&&dot&&!colon)return tw_copy(out,TW_URL,"http://",7)&&tw_copy(out+7,TW_URL-7,t,n);
    if(colon&&!space)return 0;
    const char *q=t,*e=0;int en=0,k=0;
    while(k<n&&t[k]!=' ')k++;
    if(k<n){const char *r=t+k;while(*r==' ')r++;if(*r&&(e=tw_engine(cfg,t,k,&en)))q=r;}
    if(!e&&!(e=tw_engine(cfg,"default",7,&en)))return -1;
    int at=0,plus=0;while(at<en-1&&!(e[at]=='%'&&e[at+1]=='s'))plus|=e[at++]=='?';
    if(at>=en-1){at=en;plus=0;for(int i=0;i<en;i++)plus|=e[i]=='?';}
    if(!tw_copy(out,TW_URL,e,at))return 0;
    int o=at;
    for(;*q;q++){
        u8 c=(u8)*q;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~'){if(o+1>=TW_URL)return 0;out[o++]=(char)c;}
        else if(c==' '&&plus){if(o+1>=TW_URL)return 0;out[o++]='+';}
        else {if(o+3>=TW_URL)return 0;out[o++]='%';out[o++]="0123456789ABCDEF"[c>>4];out[o++]="0123456789ABCDEF"[c&15];}
    }
    int rest=at<en?en-at-2:0;
    if(rest>0&&!tw_copy(out+o,TW_URL-o,e+at+2,rest))return 0;
    out[o+(rest>0?rest:0)]=0;return 1;
}
#define TW_VERSION "1.0"
static int tw_agent(char *out,int cap,const char *os)
{
    static const char tail[]=" textweb/" TW_VERSION;
    int n=0;while(os&&n<16&&(u8)os[n]>32&&(u8)os[n]<127)n++;
    int o=7+(n?n+1:0),t=(int)sizeof tail-1;
    if(o+t>=cap)return 0;
    tw_copy(out,cap,"FLOPNIX/",n?8:7);tw_copy(out+8,cap-8,os,n);
    return tw_copy(out+o,cap-o,tail,t);
}
static int tw_local_ref(const char *base,const char *ref,char *out)
{
    char path[TW_URL],joined[TW_URL];int n=0;
    if(ref[0]=='/'&&ref[1]=='/')return 0;
    for(int i=0;ref[i]&&ref[i]!='#'&&ref[i]!='?';i++){
        int c=(u8)ref[i];
        if(c=='%'){
            if(!ref[i+1]||!ref[i+2])return 0;
            int a=tw_hex(ref[i+1]),b=tw_hex(ref[i+2]);if(a<0||b<0)return 0;c=a*16+b;i+=2;
        }
        if(c<32||c==127||n>=TW_URL-1)return 0;path[n++]=(char)c;
    }
    path[n]=0;if(!n)return tw_local(base,out);
    if(n>1&&path[1]==':')return tw_local(path,out);
    int cut=2;if(path[0]!='/'&&path[0]!='\\')for(int i=2;base[i];i++)if(base[i]=='/')cut=i+1;
    if(cut+n>=TW_URL)return 0;
    tw_copy(joined,sizeof joined,base,cut);tw_copy(joined+cut,sizeof joined-cut,path,n);
    return tw_local(joined,out);
}
static int tw_resolve(const char *base,const char *ref,char *out)
{
    TwUrl u; char tmp[TW_URL]; int r=tw_len(ref), b=tw_len(base);
    if(!r||r>=TW_URL)return 0;
    if(tw_url(ref,&u))return tw_copy(out,TW_URL,ref,r);
    if(tw_local(base,tmp))return tw_local_ref(tmp,ref,out);
    for(int i=0;i<r&&ref[i]!='/'&&ref[i]!='?'&&ref[i]!='#';i++)if(ref[i]==':')return 0;
    if(!tw_url(base,&u)||!u.http)return 0;
    int auth=7;while(base[auth]&&base[auth]!='/'&&base[auth]!='?'&&base[auth]!='#')auth++;
    if(ref[0]=='/'&&ref[1]=='/'){
        if(r+5>=TW_URL)return 0;tw_copy(tmp,TW_URL,"http:",5);tw_copy(tmp+5,TW_URL-5,ref,r);
    }else{
        int cut=auth;
        if(ref[0]!='/'){
            cut=auth;for(int i=auth;i<b&&base[i]!='?'&&base[i]!='#';i++)if(base[i]=='/')cut=i+1;
            if(ref[0]=='?'||ref[0]=='#'){cut=auth;while(base[cut]&&base[cut]!='#'&&(ref[0]=='#'||base[cut]!='?'))cut++;}
        }
        if(cut+r+2>=TW_URL)return 0;
        tw_copy(tmp,TW_URL,base,cut);
        if(cut==auth&&ref[0]!='/')tmp[cut++]='/';
        tw_copy(tmp+cut,TW_URL-cut,ref,r);
    }
    if(!tw_url(tmp,&u))return 0;

    int a=7;while(tmp[a]&&tmp[a]!='/'&&tmp[a]!='?'&&tmp[a]!='#')a++;
    int o=a,p=a;tw_copy(out,TW_URL,tmp,a);
    while(tmp[p]&&tmp[p]!='?'&&tmp[p]!='#'){
        if(tmp[p]!='/'){out[o++]=tmp[p++];continue;}
        int e=p+1;while(tmp[e]&&tmp[e]!='/'&&tmp[e]!='?'&&tmp[e]!='#')e++;
        int len=e-p-1;
        if(len==1&&tmp[p+1]=='.'){}
        else if(len==2&&tmp[p+1]=='.'&&tmp[p+2]=='.'){while(o>a&&out[o-1]!='/')o--;if(o>a)o--;}
        else {for(int i=p;i<e;i++)out[o++]=tmp[i];}
        p=e;
    }
    if(o==a)out[o++]='/';while(tmp[p])out[o++]=tmp[p++];out[o]=0;return 1;
}
static int tw_room(TwPage *p,int n)
{
    if(p->len+n<p->text_cap)return 1;
    int cap=p->text_cap?p->text_cap:2048;while(cap<=p->len+n&&cap<TW_TEXT_MAX)cap*=2;
    char *t=p->len+n<cap&&tw_grow?tw_grow(p->text_cap?p->text:0,(u32)cap):0;if(!t)return 0;
    p->text=t;p->text_cap=cap;return 1;
}
static int tw_link_room(TwPage *p)
{
    if(p->links<p->link_cap)return 1;if(p->link_cap>=TW_LINKS_MAX||!tw_grow)return 0;
    int cap=p->link_cap?p->link_cap*2:16;TwLink *l=tw_grow(p->link,(u32)cap*sizeof *l);if(!l)return 0;
    p->link=l;p->link_cap=cap;return 1;
}
static void tw_clear(TwPage *p){p->len=p->links=p->clipped=0;if(!tw_room(p,0)){p->text=tw_blank;p->clipped=1;}p->text[0]=0;}
static void tw_free(TwPage *p)
{
    if(p->text_cap)tw_grow(p->text,0);if(p->link_cap)tw_grow(p->link,0);
    p->text=0;p->link=0;p->text_cap=p->link_cap=p->len=p->links=0;
}
static void tw_put(TwPage *p,int c,int collapse)
{
    if(c=='\r')return;
    if(c=='\t')c=' ';
    if(c<32&&c!='\n')return;
    if(collapse&&(c==' '||c=='\n')){
        if(!p->len)return;
        if(p->text[p->len-1]=='\n'||(c==' '&&p->text[p->len-1]==' '))return;
        if(c=='\n'&&p->text[p->len-1]==' ')p->len--;
    }
    if(!tw_room(p,1)){p->clipped=1;return;}p->text[p->len++]=(char)c;p->text[p->len]=0;
}
static int tw_glyph(const char *s,int n,int entities,int *used,char *out);
static int tw_put_text(TwPage *p,const char *s,int n,int entities,int collapse)
{
    char g[4];int used,k=tw_glyph(s,n,entities,&used,g);
    for(int i=0;i<k;i++)tw_put(p,(u8)g[i],collapse);
    return used;
}
static void tw_number(TwPage *p,int n)
{
    char d[6];int k=0;do{d[k++]=(char)('0'+n%10);n/=10;}while(n&&k<6);
    tw_put(p,'[',0);while(k)tw_put(p,d[--k],0);tw_put(p,']',0);tw_put(p,' ',0);
}
static int tw_addlink(TwPage *p,const char *url,const char *label,int len)
{
    TwUrl u;char local[TW_URL];if(!tw_url(url,&u)&&!tw_local(url,local))return -1;
    if(!tw_link_room(p)){p->clipped=1;return -1;}
    TwLink *l=&p->link[p->links];tw_copy(l->url,TW_URL,url,tw_len(url));l->target[0]=0;l->download=0;
    if(len>79)len=79;tw_copy(l->label,80,label,len);
    tw_number(p,p->links+1);return p->links++;
}
static const char tw_ent_names[]=
    "nbsp iexcl cent pound curren yen brvbar sect uml copy ordf laquo not shy reg macr deg plusmn "
    "sup2 sup3 acute micro para middot cedil sup1 ordm raquo frac14 frac12 frac34 iquest Agrave Aacute "
    "Acirc Atilde Auml Aring AElig Ccedil Egrave Eacute Ecirc Euml Igrave Iacute Icirc Iuml ETH Ntilde "
    "Ograve Oacute Ocirc Otilde Ouml times Oslash Ugrave Uacute Ucirc Uuml Yacute THORN szlig agrave "
    "aacute acirc atilde auml aring aelig ccedil egrave eacute ecirc euml igrave iacute icirc iuml eth "
    "ntilde ograve oacute ocirc otilde ouml divide oslash ugrave uacute ucirc uuml yacute thorn yuml "
    "ndash mdash lsquo rsquo sbquo ldquo rdquo bdquo hellip bull trade euro minus larr rarr uarr darr "
    "thinsp ensp emsp zwnj zwj lrm rlm prime Prime lsaquo rsaquo";
static const unsigned short tw_ent_more[]={0x2013,0x2014,0x2018,0x2019,0x201A,0x201C,0x201D,0x201E,
    0x2026,0x2022,0x2122,0x20AC,0x2212,0x2190,0x2192,0x2191,0x2193,0x2009,0x2002,0x2003,0x200C,0x200D,
    0x200E,0x200F,0x2032,0x2033,0x2039,0x203A};
static int tw_entity(const char *s,int *used)
{
    int n=0;while(s[n]&&s[n]!=';'&&n<10)n++;if(s[n]!=';')return 0;
    *used=n+1;
    if(n==3&&tw_ci(s,"amp",3))return '&';
    if(n==2&&tw_ci(s,"lt",2))return '<';if(n==2&&tw_ci(s,"gt",2))return '>';
    if(n==4&&tw_ci(s,"quot",4))return '"';if(n==4&&tw_ci(s,"apos",4))return '\'';
    if(s[0]=='#'){
        int i=1,v=0,base=10;if(s[i]=='x'||s[i]=='X'){base=16;i++;}
        if(i==n)return 0;
        for(;i<n;i++){int d=tw_hex(s[i]);if(d<0||d>=base)return 0;v=v*base+d;if(v>0x10FFFF)return '?';}
        return v>=32?v:' ';
    }
    const char *t=tw_ent_names;
    for(int k=0;*t;k++){
        int len=0;while(t[len]&&t[len]!=' ')len++;
        if(len==n){int i=0;while(i<n&&t[i]==s[i])i++;if(i==n)return k<96?0xA0+k:tw_ent_more[k-96];}
        t+=len;if(*t)t++;
    }
    return 0;
}
static const unsigned char tw_latin[96]={' ',0xAD,0x9B,0x9C,'?',0x9D,'|','S','"',0,0xA6,0xAE,0xAA,0,0,'-',
    0xF8,0xF1,0xFD,'3','\'',0xE6,'P',0xFA,',','1',0xA7,0xAF,0xAC,0xAB,0,0xA8,
    'A','A','A','A',0x8E,0x8F,0x92,0x80,'E',0x90,'E','E','I','I','I','I',
    'D',0xA5,'O','O','O','O',0x99,'x','O','U','U','U',0x9A,'Y',0,0xE1,
    0x85,0xA0,0x83,'a',0x84,0x86,0x91,0x87,0x8A,0x82,0x88,0x89,0x8D,0xA1,0x8C,0x8B,
    'd',0xA4,0x95,0xA2,0x93,'o',0x94,0xF6,'o',0x97,0xA3,0x96,0x81,'y',0,0x98};
static const unsigned short tw_cp1252[32]={0x20AC,'?',0x201A,0x192,0x201E,0x2026,'+','+','^','%','S',0x2039,
    0x152,'?','Z','?','?',0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,'~',0x2122,'s',0x203A,0x153,'?','z','Y'};
static const unsigned short tw_sym[]={0x3B1,0x3B2,0x393,0x3C0,0x3A3,0x3C3,0x3BC,0x3C4,0x3A6,0x398,0x3A9,0x3B4,
    0x221E,0x3C6,0x3B5,0x2229,0x2261,0x2265,0x2264,0x2248,0x221A,0x207F,0x25A0,0x192,0x2022,0x2219,0x2039,0x203A};
static const char tw_sym_cp[]="\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF\xF0\xF2\xF3\xF7\xFB\xFC\xFE\x9F\xF9\xF9<>";
/* Writes a code point as text in the CP437 screen font; returns 0 to 3 bytes. */
static int tw_uni(u32 c,char *out)
{
    const char *s="?";
    if(c<0x80){out[0]=(char)c;return 1;}
    if(c<0xA0)c=tw_cp1252[c-0x80];
    if(c<0x80){out[0]=(char)c;return 1;}
    if(c<0x100){
        if(tw_latin[c-0xA0]){out[0]=(char)tw_latin[c-0xA0];return 1;}
        s=c==0xA9?"(c)":c==0xAE?"(R)":c==0xBE?"3/4":c==0xDE?"Th":c==0xFE?"th":"";
    }else if(c>=0x2010&&c<=0x2015)s="-";
    else if(c==0x2212)s="-";
    else if(c>=0x2018&&c<=0x201B)s="'";
    else if(c>=0x201C&&c<=0x201F)s="\"";
    else if(c==0x2032)s="'";else if(c==0x2033)s="\"";
    else if(c==0x2026)s="...";else if(c==0x20AC)s="EUR";else if(c==0x2122)s="TM";
    else if(c==0x2190)s="<-";else if(c==0x2192)s="->";else if(c==0x2191)s="^";else if(c==0x2193)s="v";
    else if((c>=0x2000&&c<=0x200A)||c==0x202F||c==0x205F||c==0x3000)s=" ";
    else if((c>=0x200B&&c<=0x200F)||c==0x2060||c==0xFEFF||c==0x00AD)s="";
    else for(unsigned i=0;i<sizeof tw_sym/sizeof tw_sym[0];i++)if(tw_sym[i]==c){out[0]=tw_sym_cp[i];return 1;}
    int n=0;while(s[n]){out[n]=s[n];n++;}return n;
}
static int tw_utf8(const char *s,int n,u32 *cp)
{
    const unsigned char *u=(const unsigned char *)s;
    int k=u[0]>=0xC2&&u[0]<0xE0?2:u[0]>=0xE0&&u[0]<0xF0?3:u[0]>=0xF0&&u[0]<0xF5?4:0;
    if(!k||k>n)return 0;
    u32 c=u[0]&(0x7F>>k);
    for(int i=1;i<k;i++){if((u[i]&0xC0)!=0x80)return 0;c=c<<6|(u[i]&0x3F);}
    if((k==3&&c<0x800)||(k==4&&c<0x10000)||(c>=0xD800&&c<0xE000))return 0;
    *cp=c;return k;
}
/* An entity, UTF-8 sequence or stray Latin-1 byte becomes screen text. */
static int tw_glyph(const char *s,int n,int entities,int *used,char *out)
{
    u32 c=(unsigned char)s[0];*used=1;
    if(c=='&'&&entities){
        char e[12];int k=0;while(k<11&&k+1<n&&s[k+1]){e[k]=s[k+1];k++;if(e[k-1]==';')break;}e[k]=0;
        int v=tw_entity(e,&k);if(v){*used=k+1;c=(u32)v;}
    }else if(c>=0x80){u32 cp;int k=tw_utf8(s,n,&cp);if(k){*used=k;c=cp;}}
    return tw_uni(c,out);
}

static void tw_render(TwPage *p,const char *s,int mode,const char *base)
{
    tw_clear(p);
    if(mode>=2){
        while(*s){
            const char *end=s;while(*end&&*end!='\n'&&*end!='\r')end++;int n=(int)(end-s);
            if(n==1&&s[0]=='.')break;
            if(mode==3){int i=n>1&&s[0]=='.'&&s[1]=='.';while(i<n)i+=tw_put_text(p,s+i,n-i,0,0);}
            else if(n){
                int tab[4],nt=0;for(int i=1;i<n&&nt<4;i++)if(s[i]=='\t')tab[nt++]=i;
                int label=nt?tab[0]-1:n-1;
                if(nt>=3&&(s[0]=='0'||s[0]=='1'||s[0]=='7'||s[0]=='h'||s[0]=='9'||s[0]=='5'||s[0]=='4'||s[0]=='6'||s[0]=='g'||s[0]=='I'||s[0]=='s')){
                    char host[80],sel[176],enc[530],url[TW_URL];int pe=nt>3?tab[3]:n,ok=1;u32 port=0;
                    ok=tw_copy(host,80,s+tab[1]+1,tab[2]-tab[1]-1)&&tw_copy(sel,176,s+tab[0]+1,tab[1]-tab[0]-1);
                    for(int i=tab[2]+1;i<pe;i++){if(s[i]<'0'||s[i]>'9'){ok=0;break;}port=port*10+s[i]-'0';if(port>65535){ok=0;break;}}
                    if(ok&&port&&tw_encoded(enc,sizeof enc,sel)){
                        int o=0;const char *pre="gopher://";while(*pre)url[o++]=*pre++;
                        int hn=tw_len(host),en=tw_len(enc);
                        if(hn+en+o+10<TW_URL){
                            for(int i=0;i<hn;i++)url[o++]=host[i];url[o++]=':';
                            char digits[5];int k=0;do{digits[k++]=(char)('0'+port%10);port/=10;}while(port);
                            while(k)url[o++]=digits[--k];url[o++]='/';url[o++]=s[0];
                            for(int i=0;i<=en;i++)url[o+i]=enc[i];
                            if(s[0]=='h'&&tw_len(sel)>4&&tw_ci(sel,"url:",4))tw_copy(url,TW_URL,sel+4,tw_len(sel+4));
                            tw_addlink(p,url,s+1,label);
                        }else p->clipped=1;
                    }
                }
                for(int i=0;i<label;)i+=tw_put_text(p,s+1+i,label-i,0,0);
            }
            tw_put(p,'\n',0);s=end;if(*s=='\r')s++;if(*s=='\n')s++;
        }
        return;
    }
    int skip=0,anchor=-1,anchor_start=0;
    for(int i=0;s[i];){
        if(mode==1&&s[i]=='<'){
            int e=i+1,quote=0;for(;s[e];e++){if(quote){if(s[e]==quote)quote=0;}else if(s[e]=='\''||s[e]=='"')quote=s[e];else if(s[e]=='>')break;}
            if(!s[e])break;
            if(s[i+1]=='!'&&s[i+2]=='-'&&s[i+3]=='-'){
                e=i+4;while(s[e]&&!(s[e]=='-'&&s[e+1]=='-'&&s[e+2]=='>'))e++;
                i=s[e]?e+3:e;continue;
            }
            int a=i+1,closing=0;if(s[a]=='/'){closing=1;a++;}
            int z=a;while(z<e&&((s[z]>='a'&&s[z]<='z')||(s[z]>='A'&&s[z]<='Z')||(s[z]>='0'&&s[z]<='9')))z++;
            int len=z-a,is_script=len==6&&tw_ci(s+a,"script",6),is_style=len==5&&tw_ci(s+a,"style",5);
            if(is_script||is_style){if(closing)skip=0;else skip=1;}
            if(!skip){
                if((len==1&&(tw_ci(s+a,"p",1)||tw_ci(s+a,"h",1)))||(len==2&&(tw_ci(s+a,"br",2)||tw_ci(s+a,"li",2)||tw_ci(s+a,"tr",2)||(s[a]=='h'&&s[a+1]>='1'&&s[a+1]<='6')))||(len==3&&tw_ci(s+a,"div",3)))tw_put(p,'\n',1);
                if(len==1&&tw_ci(s+a,"a",1)){
                    if(closing&&anchor>=0){int n=p->len-anchor_start;if(n>79)n=79;tw_copy(p->link[anchor].label,80,p->text+anchor_start,n);anchor=-1;}
                    else if(!closing){
                        for(int q=z;q+4<e;q++)if((s[q]==' '||s[q]=='\t'||s[q]=='\n')&&tw_ci(s+q+1,"href",4)){
                            q+=5;while(q<e&&s[q]==' ')q++;if(s[q]!='=')continue;q++;while(q<e&&s[q]==' ')q++;
                            int qt=0;if(s[q]=='\''||s[q]=='"')qt=s[q++];int b=q;
                            while(q<e&&(qt?s[q]!=qt:s[q]!=' '&&s[q]!='\t'))q++;
                            char ref[TW_URL],url[TW_URL];
                            if(tw_copy(ref,TW_URL,s+b,q-b)&&tw_resolve(base,ref,url)){
                                anchor=tw_addlink(p,url,ref,tw_len(ref));anchor_start=p->len;
                            }break;
                        }
                    }
                }
            }
            i=e+1;continue;
        }
        if(!skip){
            int c=(u8)s[i];
            if(c>=0x80||(mode==1&&c=='&')){i+=tw_put_text(p,s+i,0x7FFFFFFF,mode==1,mode==1);continue;}
            if(mode==1&&(c=='\n'||c=='\r'||c=='\t'))c=' ';tw_put(p,c,mode==1);
        }
        i++;
    }
}

static int tw_wrap(const char *s,int off,int cols)
{
    int p=off,last=-1;if(cols<1)cols=1;
    while(s[p]&&s[p]!='\n'&&p-off<cols){if(s[p]==' ')last=p;p++;}
    if(s[p]=='\n'||s[p]==' ')return p+1;if(!s[p])return p;
    if(last>=off)return last+1;return p;
}
