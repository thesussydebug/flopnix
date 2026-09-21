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
    u16 sum=0;for(u32 i=0;i<n;i+=2){u16 w=i==4?0:(u16)(p[i]|p[i+1]<<8);sum=(u16)(((sum<<1)|(sum>>15))+w);}
    if(sum!=(u16)(p[4]|p[5]<<8))return 0;
    return ku_crc_feed(0,p,n)==r->crc;
}
