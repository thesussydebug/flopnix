typedef struct { unsigned start,end; } TfLine;
static int tf_space(unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';}
static unsigned tf_end(const unsigned char *p,unsigned n,unsigned at){while(at<n&&p[at]!='\n')at++;return at;}
static unsigned tf_count(const unsigned char *p,unsigned n){unsigned k=0;for(unsigned i=0;i<n;i++)if(p[i]=='\n')k++;return k+(n&&p[n-1]!='\n');}
static unsigned tf_lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int tf_match(const unsigned char *p,unsigned n,const char *s,int fold)
{
    unsigned len=0;while(s[len])len++;if(len>n)return 0;
    for(unsigned i=0;i<=n-len;i++){unsigned j=0;while(j<len&&(fold?tf_lower(p[i+j])==tf_lower((unsigned char)s[j]):p[i+j]==(unsigned char)s[j]))j++;if(j==len)return 1;}return 0;
}
static int tf_compare(const unsigned char *p,TfLine a,TfLine b,int numeric)
{
    unsigned x=a.start,y=b.start;
    if(numeric){
        while(x<a.end&&tf_space(p[x]))x++;while(y<b.end&&tf_space(p[y]))y++;
        int na=x<a.end&&p[x]=='-',nb=y<b.end&&p[y]=='-';
        if(x<a.end&&(p[x]=='-'||p[x]=='+'))x++;if(y<b.end&&(p[y]=='-'||p[y]=='+'))y++;
        while(x<a.end&&p[x]=='0')x++;while(y<b.end&&p[y]=='0')y++;
        unsigned xe=x,ye=y;while(xe<a.end&&p[xe]>='0'&&p[xe]<='9')xe++;while(ye<b.end&&p[ye]>='0'&&p[ye]<='9')ye++;
        if(xe==x)na=0;if(ye==y)nb=0;if(na!=nb)return na?-1:1;
        int result=0;if(xe-x!=ye-y)result=xe-x<ye-y?-1:1;
        else while(x<xe){if(p[x]!=p[y]){result=p[x]<p[y]?-1:1;break;}x++;y++;}
        if(result)return na?-result:result;
        return 0;
    }
    while(x<a.end&&y<b.end){if(p[x]!=p[y])return p[x]<p[y]?-1:1;x++;y++;}
    return (x<a.end)-(y<b.end);
}
static void tf_sift(const unsigned char *p,TfLine *a,unsigned root,unsigned n,int numeric,int reverse)
{
    while(root<n/2){unsigned child=root*2+1;
        int sign=reverse?-1:1;
        if(child+1<n&&sign*tf_compare(p,a[child],a[child+1],numeric)<0)child++;
        if(sign*tf_compare(p,a[root],a[child],numeric)>=0)return;
        TfLine t=a[root];a[root]=a[child];a[child]=t;root=child;
    }
}
static void tf_sort(const unsigned char *p,TfLine *a,unsigned n,int numeric,int reverse)
{
    for(unsigned i=n/2;i;i--)tf_sift(p,a,i-1,n,numeric,reverse);
    for(unsigned i=n;i>1;i--){TfLine t=a[0];a[0]=a[i-1];a[i-1]=t;tf_sift(p,a,0,i-1,numeric,reverse);}
}
static unsigned tf_crc32(const unsigned char *p,unsigned n)
{
    unsigned crc=~0u;for(unsigned i=0;i<n;i++){crc^=p[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;
}
