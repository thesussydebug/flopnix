#pragma once
#include "nethttp.h"
typedef struct {
    NetHttpInfo info;
    char header[4096],line[256];
    u32 received,left,trailer;
    int hn,ln,state,error,interim,chunked;
    u8 http11,close_hdr,keep_hdr;
} HttpReader;
static int hr_lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int hr_length(const char *s){int n=0;while(s[n])n++;return n;}
static int hr_token(int c)
{
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))return 1;
    const char *p="!#$%&'*+-.^_`|~";while(*p)if(c==*p++)return 1;return 0;
}
static int hr_equal(const char *a,int n,const char *b)
{
    int i=0;while(b[i]){if(i>=n||hr_lower((u8)a[i])!=b[i])return 0;i++;}return i==n;
}
static int hr_copy(char *d,int cap,const char *s,int n)
{
    if(n>=cap)return 0;for(int i=0;i<n;i++)d[i]=s[i];d[n]=0;return 1;
}
static int hr_decimal(const char *s,int n,u32 *out)
{
    if(!n)return 0;u32 v=0;
    for(int i=0;i<n;i++){if(s[i]<'0'||s[i]>'9'||v>(0xffffffffu-(u32)(s[i]-'0'))/10)return 0;v=v*10+(u32)(s[i]-'0');}
    *out=v;return 1;
}
static void hr_init(HttpReader *r)
{
    u8 *p=(u8 *)r;for(u32 i=0;i<sizeof *r;i++)p[i]=0;
}
static int hr_has_token(const char *s,int n,const char *token)
{
    int t=hr_length(token);
    for(int i=0;i+t<=n;i++){
        if((i&&s[i-1]!=','&&s[i-1]!=' '&&s[i-1]!='\t')||(i+t<n&&s[i+t]!=','&&s[i+t]!=' '&&s[i+t]!='\t'))continue;
        int k=0;while(k<t&&hr_lower((u8)s[i+k])==token[k])k++;if(k==t)return 1;
    }
    return 0;
}
static int hr_headers(HttpReader *r)
{
    char *h=r->header;int end=0;while(end<r->hn&&h[end]!='\r')end++;
    if(end<12||h[0]!='H'||h[1]!='T'||h[2]!='T'||h[3]!='P'||h[4]!='/'||h[5]!='1'||h[6]!='.'||(h[7]!='0'&&h[7]!='1')||h[8]!=' ')return 0;
    int status=0;for(int i=9;i<12;i++){if(h[i]<'0'||h[i]>'9')return 0;status=status*10+h[i]-'0';}
    if(status<100||(end>12&&h[12]!=' ')||end+1>=r->hn||h[end+1]!='\n')return 0;
    for(int i=12;i<end;i++)if(((u8)h[i]<32&&h[i]!='\t')||h[i]==127)return 0;
    r->info.status=status;r->http11=h[7]=='1';r->close_hdr=r->keep_hdr=0;
    int have_length=0,have_te=0;
    for(int p=end+2;p<r->hn-2;){
        end=p;while(end<r->hn&&h[end]!='\r')end++;
        if(end+1>=r->hn||h[end+1]!='\n'||end==p)return 0;
        int colon=p;while(colon<end&&h[colon]!=':'){
            if(!hr_token((u8)h[colon]))return 0;colon++;
        }
        if(colon==p||colon==end)return 0;
        int a=colon+1,b=end;while(a<b&&(h[a]==' '||h[a]=='\t'))a++;while(b>a&&(h[b-1]==' '||h[b-1]=='\t'))b--;
        for(int i=a;i<b;i++)if(((u8)h[i]<32&&h[i]!='\t')||h[i]==127)return 0;
        int n=b-a;
        if(hr_equal(h+p,colon-p,"content-length")){
            u32 value;if(!hr_decimal(h+a,n,&value)||(have_length&&value!=r->info.length))return 0;
            r->info.length=value;r->info.length_known=1;have_length=1;
        }else if(hr_equal(h+p,colon-p,"transfer-encoding")){
            if(have_te||!hr_equal(h+a,n,"chunked"))return 0;have_te=1;r->chunked=1;
        }else if(hr_equal(h+p,colon-p,"content-type")){
            if(!hr_copy(r->info.type,sizeof r->info.type,h+a,n))return 0;
        }else if(hr_equal(h+p,colon-p,"connection")){
            if(hr_has_token(h+a,n,"close"))r->close_hdr=1;
            if(hr_has_token(h+a,n,"keep-alive"))r->keep_hdr=1;
        }else if(hr_equal(h+p,colon-p,"location")){
            if(!hr_copy(r->info.location,sizeof r->info.location,h+a,n))return 0;
        }else if(hr_equal(h+p,colon-p,"content-encoding")){
            if(!hr_copy(r->info.encoding,sizeof r->info.encoding,h+a,n))return 0;
        }else if(hr_equal(h+p,colon-p,"content-disposition")){
            for(int i=a;i+9<=b;i++)if((i==a||h[i-1]==';'||h[i-1]==' ')&&hr_equal(h+i,9,"filename=")){
                i+=9;int quote=i<b&&h[i]=='"';if(quote)i++;int start=i;
                while(i<b&&(quote?h[i]!='"':h[i]!=';'&&h[i]!=' '))i++;
                if(!hr_copy(r->info.filename,sizeof r->info.filename,h+start,i-start))r->info.filename[0]=0;break;
            }
        }
        p=end+2;
    }
    if(have_te&&have_length)return 0;
    if(status<200){
        if(status==101||++r->interim>4||have_te||have_length)return 0;
        r->hn=0;r->info.status=0;r->info.type[0]=r->info.location[0]=r->info.filename[0]=r->info.encoding[0]=0;return 1;
    }
    if(r->info.encoding[0]&&!hr_equal(r->info.encoding,hr_length(r->info.encoding),"identity")){r->error=-6;return 0;}
    if(status==204||status==304){r->state=7;return 1;}
    if(r->chunked)r->state=3;
    else if(have_length){r->left=r->info.length;r->state=r->left?1:7;}
    else r->state=2;
    return 1;
}
static int hr_feed(HttpReader *r,const u8 *s,int n,int (*sink)(const u8 *,int,void *),void *ctx)
{
    if(n<0||r->error)return 0;
    for(int p=0;p<n;){
        if(r->state==7)return 1;
        if(r->state==0){
            if(r->hn>=(int)sizeof r->header)return 0;
            char c=(char)s[p++];if(!c)return 0;r->header[r->hn++]=c;
            if(r->hn>=4&&r->header[r->hn-4]=='\r'&&r->header[r->hn-3]=='\n'&&r->header[r->hn-2]=='\r'&&c=='\n')if(!hr_headers(r))return 0;
        }else if(r->state==1||r->state==2||r->state==4){
            u32 take=(u32)(n-p);if(r->state!=2&&take>r->left)take=r->left;
            if(take>0xffffffffu-r->received)return 0;
            if(take&&sink&&!sink(s+p,(int)take,ctx))return 0;
            p+=(int)take;r->received+=take;
            if(r->state!=2){r->left-=take;if(!r->left)r->state=r->state==1?7:5;}
        }else if(r->state==5){if(s[p++]!='\r')return 0;r->state=8;}
        else if(r->state==8){if(s[p++]!='\n')return 0;r->state=3;}
        else if(r->state==3||r->state==6){
            if(r->ln>=(int)sizeof r->line)return 0;u8 c=s[p++];if(!c)return 0;r->line[r->ln++]=(char)c;
            if(r->state==6&&++r->trailer>4096)return 0;
            if(c=='\n'){
                if(r->ln<2||r->line[r->ln-2]!='\r')return 0;
                if(r->state==6){if(r->ln==2)r->state=7;else {int colon=0;for(int i=0;i<r->ln-2;i++){if(r->line[i]==':')colon=1;if((u8)r->line[i]<32&&r->line[i]!='\t')return 0;}if(!colon)return 0;}}
                else {
                    u32 value=0;int i=0,lim=r->ln-2;
                    for(;i<lim;i++){int c=hr_lower(r->line[i]),d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;if(d<0)break;if(value>(0xffffffffu-(u32)d)/16)return 0;value=value*16+(u32)d;}
                    if(!i||(i<lim&&r->line[i]!=';'))return 0;
                    for(;i<lim;i++)if((u8)r->line[i]<32||r->line[i]==127)return 0;
                    r->left=value;r->state=value?4:6;
                }
                r->ln=0;
            }
        }else return 0;
    }
    return 1;
}
static int hr_finish(const HttpReader *r){return !r->error&&(r->state==7||r->state==2);}
static int hr_reusable(const HttpReader *r){return r->state==7&&!r->error&&!r->close_hdr&&(r->http11||r->keep_hdr);}
