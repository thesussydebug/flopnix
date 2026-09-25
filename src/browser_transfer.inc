#pragma once
static int br_redirect(int status){return status==301||status==302||status==303||status==307||status==308;}
static u32 br_gopher_text(u8 *data,u32 len)
{
    u32 p=0,out=0;
    while(p<len){
        u32 end=p;while(end<len&&data[end]!='\r'&&data[end]!='\n')end++;
        if(end-p==1&&data[p]=='.')break;
        if(end-p>=2&&data[p]=='.'&&data[p+1]=='.')p++;
        while(p<end)data[out++]=data[p++];
        if(p<len&&data[p]=='\r')data[out++]=data[p++];
        if(p<len&&data[p]=='\n')data[out++]=data[p++];
    }
    data[out]=0;return out;
}
static int br_media(const char *type,const char *wanted)
{
    int n=tw_len(wanted);return tw_len(type)>=n&&tw_ci(type,wanted,n)&&(!type[n]||type[n]==';'||type[n]==' ');
}
static int br_mode(const TwUrl *u,const NetHttpInfo *info,const u8 *data,int length)
{
    if(!u->http){if(u->type=='1'||u->type=='7')return 2;if(u->type=='0')return 3;if(u->type=='h')return 1;return -1;}
    if(info->filename[0])return -1;
    if(br_media(info->type,"text/html")||br_media(info->type,"application/xhtml+xml"))return 1;
    if(info->type[0]&&!br_media(info->type,"text/plain")&&!br_media(info->type,"text/css"))return -1;
    for(int i=0;i<length;i++)if(!data[i]||(data[i]<9)||(data[i]>13&&data[i]<32))return -1;
    if(!info->type[0]){int i=0;while(i<length&&br_space(data[i]))i++;if(i<length&&data[i]=='<')return 1;}
    return 0;
}
static void br_filename(char out[13],const char *url,const char *suggested,int html)
{
    const char *s=suggested&&*suggested?suggested:url;int a=0,end=0;
    while(s[end]&&s[end]!='?'&&s[end]!='#'){if(s[end]=='/'||s[end]=='\\'||s[end]==':')a=end+1;end++;}
    int dot=-1;for(int i=a;i<end;i++)if(s[i]=='.')dot=i;
    int n=0,stem=dot>a?dot:end;
    for(int i=a;i<stem&&n<8;i++){int c=(u8)s[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')out[n++]=(char)c;}
    if(!n){tw_copy(out,13,"download",8);n=8;}
    int ext=n;if(dot>a){for(int i=dot+1;i<end&&n-ext<4;i++){int c=(u8)s[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')){if(n==ext)out[n++]='.';out[n++]=(char)c;}}}
    if(n==ext&&html){out[n++]='.';out[n++]='h';out[n++]='t';out[n++]='m';}
    out[n]=0;
}
static int br_usb_name(const char *path)
{
    const char *name=path;for(const char *p=path;*p;p++)if(*p=='/')name=p+1;
    int stem=0,ext=0,dot=0;
    for(;*name;name++){
        int c=(u8)*name;if(c=='.'){if(dot||!stem)return 0;dot=1;continue;}
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
        if(dot){if(++ext>3)return 0;}else if(++stem>8)return 0;
    }
    return stem&&(!dot||ext);
}
