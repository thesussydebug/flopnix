#pragma once
static inline u32 ku_crc_feed(u32 crc,const u8 *p,u32 n)
{
    static const u32 table[16]={
        0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
        0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    crc=~crc;
    while(n--){crc^=*p++;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}
    return ~crc;
}
static inline int ku_equal(const u8 *a,const u8 *b,u32 n){u32 diff=0;for(u32 i=0;i<n;i++)diff|=a[i]^b[i];return !diff;}
static inline u32 ku_u32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static inline int ku_hexval(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline int ku_unhex(const char *p,u8 *out,u32 n){for(u32 i=0;i<n;i++){int a=ku_hexval(p[i*2]),b=ku_hexval(p[i*2+1]);if(a<0||b<0)return 0;out[i]=(u8)(a*16+b);}return p[n*2]==0;}
static inline void ku_hex(const u8 *p,u32 n,char *out){const char *h="0123456789abcdef";for(u32 i=0;i<n;i++){out[i*2]=h[p[i]>>4];out[i*2+1]=h[p[i]&15];}out[n*2]=0;}
#define KU_MANIFEST 24
static inline int ku_pair_key(const char *text,u8 *out)
{
    char p[33];u32 n=0;
    while(*text){char c=*text++;if(c==' '||c=='-')continue;if(n==32)return 0;p[n++]=c;}
    p[n]=0;if(n==32)return ku_unhex(p,out,16);if(n!=26&&n!=8)return 0;
    for(u32 i=0;i<16;i++)out[i]=0;
    u32 bits=0,value=0,j=0;
    for(u32 i=0;i<n;i++){
        char c=p[i];if(c>='a'&&c<='z')c-=32;
        int v=c>='A'&&c<='Z'?c-'A':c>='2'&&c<='7'?c-'2'+26:-1;
        if(v<0)return 0;value=(value<<5)|(u32)v;bits+=5;
        if(bits>=8){bits-=8;out[j++]=(u8)(value>>bits);}
    }
    return n==8?j==5:j==16&&!(value&3);
}
static inline void ku_pair_text(const u8 *key,char *out)
{
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";u32 bits=0,value=0,n=0,j=0;
    u32 len=5;for(u32 i=5;i<16;i++)if(key[i])len=16;
    for(u32 i=0;i<len;i++){
        value=(value<<8)|key[i];bits+=8;
        while(bits>=5){bits-=5;if(len==16&&n&&!(n%4))out[j++]='-';out[j++]=alphabet[(value>>bits)&31];n++;}
    }
    if(bits)out[j++]=alphabet[(value<<(5-bits))&31];out[j]=0;
}
#define KU_MAX (255u*512u)
typedef struct {u32 sequence,size,abi,crc;} KuRelease;
static inline u32 ku_pair_check(const u8 *key,const u8 *header)
{
    return ku_crc_feed(ku_crc_feed(0,header,20),key,16);
}
static inline int ku_manifest(const u8 *p,u32 n,const u8 *key,u32 floor,u32 last,u32 abi,KuRelease *r)
{
    if(n!=KU_MANIFEST||p[0]!='F'||p[1]!='X'||p[2]!='U'||p[3]!='2')return 0;
    if(ku_pair_check(key,p)!=ku_u32(p+20))return 0;
    u32 seq=ku_u32(p+4),size=ku_u32(p+8),target=ku_u32(p+12),crc=ku_u32(p+16);
    if(!seq||seq<floor||(seq==floor&&crc!=last))return 0;
    if(size<4096||size>KU_MAX||(size&511)||target<abi||target>65535)return 0;
    r->sequence=seq;r->size=size;r->abi=target;r->crc=crc;return 1;
}
static inline int ku_image(const u8 *p,u32 n,const KuRelease *r)
{
    if(n!=r->size||n<4096||n>KU_MAX||(n&511)||p[0]!=0xeb||(u32)(p[6]|p[7]<<8)!=n/512)return 0;
    if(ku_equal(p+8,(const u8 *)"FXK1",4)&&ku_u32(p+12)!=r->abi)return 0;
    u16 sum=0;for(u32 i=0;i<n;i+=2){u16 w=i==4?0:(u16)(p[i]|p[i+1]<<8);sum=(u16)(((sum<<1)|(sum>>15))+w);}
    if(sum!=(u16)(p[4]|p[5]<<8))return 0;
    return ku_crc_feed(0,p,n)==r->crc;
}

typedef struct {u32 h[8],len;u8 buf[64];} KuSha;
static inline u32 ku_ror(u32 x,u32 n){return x>>n|x<<(32-n);}
static inline void ku_sha_block(u32 *h,const u8 *p)
{
    static const u32 k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    u32 w[64],v[8];
    for(u32 i=0;i<16;i++)w[i]=(u32)p[i*4]<<24|(u32)p[i*4+1]<<16|(u32)p[i*4+2]<<8|p[i*4+3];
    for(u32 i=16;i<64;i++){
        u32 a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(ku_ror(a,7)^ku_ror(a,18)^(a>>3))+w[i-7]+(ku_ror(b,17)^ku_ror(b,19)^(b>>10));
    }
    for(u32 i=0;i<8;i++)v[i]=h[i];
    for(u32 i=0;i<64;i++){
        u32 t1=v[7]+(ku_ror(v[4],6)^ku_ror(v[4],11)^ku_ror(v[4],25))+((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
        u32 t2=(ku_ror(v[0],2)^ku_ror(v[0],13)^ku_ror(v[0],22))+((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
        for(u32 j=7;j;j--)v[j]=v[j-1];
        v[4]+=t1;v[0]=t1+t2;
    }
    for(u32 i=0;i<8;i++)h[i]+=v[i];
}
static inline void ku_sha_init(KuSha *s)
{
    static const u32 iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(u32 i=0;i<8;i++)s->h[i]=iv[i];
    s->len=0;
}
static inline void ku_sha_feed(KuSha *s,const u8 *p,u32 n)
{
    while(n--){s->buf[s->len++&63]=*p++;if(!(s->len&63))ku_sha_block(s->h,s->buf);}
}
static inline void ku_sha_done(KuSha *s,u8 *out)
{
    u32 hi=s->len>>29,lo=s->len<<3;
    ku_sha_feed(s,(const u8 *)"\x80",1);
    while((s->len&63)!=56)ku_sha_feed(s,(const u8 *)"",1);
    u8 t[8]={(u8)(hi>>24),(u8)(hi>>16),(u8)(hi>>8),(u8)hi,(u8)(lo>>24),(u8)(lo>>16),(u8)(lo>>8),(u8)lo};
    ku_sha_feed(s,t,8);
    for(u32 i=0;i<32;i++)out[i]=(u8)(s->h[i>>2]>>(24-8*(i&3)));
}
static inline void ku_hmac(const u8 *key,const u8 *msg,u32 n,u8 *out)
{
    u8 pad[64],in[32];KuSha s;
    for(u32 i=0;i<64;i++)pad[i]=(u8)((i<16?key[i]:0)^0x36);
    ku_sha_init(&s);ku_sha_feed(&s,pad,64);ku_sha_feed(&s,msg,n);ku_sha_done(&s,in);
    for(u32 i=0;i<64;i++)pad[i]^=0x36^0x5c;
    ku_sha_init(&s);ku_sha_feed(&s,pad,64);ku_sha_feed(&s,in,32);ku_sha_done(&s,out);
}
