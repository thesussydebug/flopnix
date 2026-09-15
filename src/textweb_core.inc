/* Parses text-browser addresses and responses. */
#pragma once
#define TW_URL 256
#define TW_TEXT 24576
#define TW_LINKS 48
typedef struct { char host[80], path[176]; u16 port; u8 http, type; } TwUrl;
typedef struct { char url[TW_URL], label[80]; } TwLink;
typedef struct { char text[TW_TEXT]; TwLink link[TW_LINKS]; int len, links, clipped; } TwPage;
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

static int tw_resolve(const char *base,const char *ref,char *out)
{
    TwUrl u; char tmp[TW_URL]; int r=tw_len(ref), b=tw_len(base);
    if(!r||r>=TW_URL)return 0;
    if(tw_url(ref,&u))return tw_copy(out,TW_URL,ref,r);
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
    if(p->len>=TW_TEXT-1){p->clipped=1;return;}p->text[p->len++]=(char)c;p->text[p->len]=0;
}
static void tw_number(TwPage *p,int n)
{
    tw_put(p,'[',0);if(n>=10)tw_put(p,'0'+n/10,0);tw_put(p,'0'+n%10,0);tw_put(p,']',0);tw_put(p,' ',0);
}
static int tw_addlink(TwPage *p,const char *url,const char *label,int len)
{
    TwUrl u;if(!tw_url(url,&u))return -1;
    if(p->links>=TW_LINKS){p->clipped=1;return -1;}
    TwLink *l=&p->link[p->links];tw_copy(l->url,TW_URL,url,tw_len(url));
    if(len>79)len=79;tw_copy(l->label,80,label,len);
    tw_number(p,p->links+1);return p->links++;
}
static int tw_entity(const char *s,int *used)
{
    int n=0;while(s[n]&&s[n]!=';'&&n<10)n++;if(s[n]!=';')return 0;
    *used=n+1;
    if(n==3&&tw_ci(s,"amp",3))return '&';
    if(n==2&&tw_ci(s,"lt",2))return '<';if(n==2&&tw_ci(s,"gt",2))return '>';
    if(n==4&&tw_ci(s,"quot",4))return '"';if(n==4&&tw_ci(s,"apos",4))return '\'';
    if(n==4&&tw_ci(s,"nbsp",4))return ' ';
    if(s[0]=='#'){
        int i=1,v=0,base=10;if(s[i]=='x'||s[i]=='X'){base=16;i++;}
        if(i==n)return 0;
        for(;i<n;i++){int d=tw_hex(s[i]);if(d<0||d>=base)return 0;v=v*base+d;if(v>255)return '?';}
        return v>=32?v:' ';
    }
    return 0;
}

static void tw_render(TwPage *p,const char *s,int mode,const char *base)
{
    p->len=p->links=p->clipped=0;p->text[0]=0;
    if(mode>=2){
        while(*s){
            const char *end=s;while(*end&&*end!='\n'&&*end!='\r')end++;int n=(int)(end-s);
            if(n==1&&s[0]=='.')break;
            if(mode==3){int i=n>1&&s[0]=='.'&&s[1]=='.';for(;i<n;i++)tw_put(p,(u8)s[i],0);}
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
                for(int i=0;i<label;i++)tw_put(p,(u8)s[1+i],0);
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
            if(mode==1&&s[i]=='&'){int used=0,c=tw_entity(s+i+1,&used);if(c){tw_put(p,c,1);i+=used+1;continue;}}
            int c=(u8)s[i];if(mode==1&&(c=='\n'||c=='\r'||c=='\t'))c=' ';tw_put(p,c,mode==1);
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
