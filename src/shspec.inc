#include "shcwd.inc"
static inline int sp_token(const char **input,char *out,int cap)
{
    const char *p=*input;while(*p==' '||*p=='\t')p++;
    int quote=(*p=='"'||*p=='\'')?*p++:0,n=0;
    while(*p&&(quote?*p!=quote:*p!=' '&&*p!='\t')){
        if((unsigned char)*p<32||n>=cap-1){out[0]=0;return -1;}out[n++]=*p++;
    }
    if(quote){if(*p!=quote){out[0]=0;return -1;}p++;if(*p&&*p!=' '&&*p!='\t'){out[0]=0;return -1;}}
    out[n]=0;while(*p==' '||*p=='\t')p++;*input=p;return n;
}
static inline int sp_resolve(const char *cwd,const char *arg,int *drive,char *out,int cap)
{
    const char *base=cwd?cwd:"",*p=arg?arg:"";int d=0;
    if(base[0]&&base[1]==':'){d=base[0]=='u'||base[0]=='U';base+=2;}
    if(p[0]&&p[1]==':'){
        int nd=p[0]=='u'||p[0]=='U';if(!nd&&p[0]!='a'&&p[0]!='A')return 0;
        d=nd;base="";p+=2;
    }
    if(cap<2)return 0;
    if(!sc_resolve(base,p,out+(d?1:0),cap-(d?1:0)))return 0;
    if(d)out[0]='/';
    int n=0;for(;out[n];n++)if(out[n]==':'||out[n]=='"'||(unsigned char)out[n]<32)return 0;
    if(!d&&n>=24)return 0;*drive=d;return 1;
}
static inline int sp_insert(char *line,int len,int cap,const char *list)
{
    while(*list){
        const char *p=list;int n=0,quote=0,bad=0;
        while(*list&&*list!='\n'){if(*list==' ')quote=1;if((unsigned char)*list<32||*list=='"')bad=1;n++;list++;}
        if(*list)list++;
        if(!n||bad)continue;
        int gap=len&&line[len-1]!=' ';
        if(len+gap+n+2*quote>=cap)break;
        if(gap)line[len++]=' ';if(quote)line[len++]='"';
        for(int i=0;i<n;i++)line[len++]=p[i];if(quote)line[len++]='"';
    }
    line[len]=0;return len;
}
