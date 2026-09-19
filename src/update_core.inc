#pragma once
typedef struct { u32 h[8], bytes, used; u8 block[64]; } KuSha;
static inline u32 ku_rotr(u32 x,int n){return (x>>n)|(x<<(32-n));}
static inline void ku_sha_block(KuSha *s,const u8 *p)
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
    u32 w[64];for(int i=0;i<16;i++)w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|p[i*4+3];
    for(int i=16;i<64;i++){u32 a=w[i-15],b=w[i-2];w[i]=w[i-16]+(ku_rotr(a,7)^ku_rotr(a,18)^(a>>3))+w[i-7]+(ku_rotr(b,17)^ku_rotr(b,19)^(b>>10));}
    u32 a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<64;i++){
        u32 t=h+(ku_rotr(e,6)^ku_rotr(e,11)^ku_rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        u32 q=(ku_rotr(a,2)^ku_rotr(a,13)^ku_rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+q;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static inline void ku_sha_init(KuSha *s)
{
    static const u32 initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(int i=0;i<8;i++)s->h[i]=initial[i];s->used=s->bytes=0;
}
static inline void ku_sha_feed(KuSha *s,const u8 *p,u32 n)
{
    s->bytes+=n;
    while(n--){s->block[s->used++]=*p++;if(s->used==64){ku_sha_block(s,s->block);s->used=0;}}
}
static inline void ku_sha_done(KuSha *s,u8 *out)
{
    u32 count=s->bytes;u8 c=128;ku_sha_feed(s,&c,1);c=0;
    while(s->used!=56)ku_sha_feed(s,&c,1);
    u8 len[8]={0,0,0,(u8)(count>>29),(u8)(count>>21),(u8)(count>>13),(u8)(count>>5),(u8)(count<<3)};
    ku_sha_feed(s,len,8);for(int i=0;i<32;i++)out[i]=(u8)(s->h[i/4]>>(24-(i%4)*8));
}
static inline void ku_sha(const u8 *p,u32 n,u8 *out){KuSha s;ku_sha_init(&s);ku_sha_feed(&s,p,n);ku_sha_done(&s,out);}
static inline void ku_hmac(const u8 *key,u32 keylen,const u8 *data,u32 n,u8 *out)
{
    u8 pad[64],digest[32];KuSha s;
    if(keylen>64){ku_sha(key,keylen,digest);key=digest;keylen=32;}
    for(int i=0;i<64;i++)pad[i]=(i<(int)keylen?key[i]:0)^0x36;
    ku_sha_init(&s);ku_sha_feed(&s,pad,64);ku_sha_feed(&s,data,n);ku_sha_done(&s,out);
    for(int i=0;i<64;i++)pad[i]^=0x36^0x5c;
    ku_sha_init(&s);ku_sha_feed(&s,pad,64);ku_sha_feed(&s,out,32);ku_sha_done(&s,out);
}
static inline int ku_equal(const u8 *a,const u8 *b,u32 n){u32 diff=0;for(u32 i=0;i<n;i++)diff|=a[i]^b[i];return !diff;}
static inline u32 ku_u32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static inline int ku_hexval(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline int ku_unhex(const char *p,u8 *out,u32 n){for(u32 i=0;i<n;i++){int a=ku_hexval(p[i*2]),b=ku_hexval(p[i*2+1]);if(a<0||b<0)return 0;out[i]=(u8)(a*16+b);}return p[n*2]==0;}
static inline void ku_hex(const u8 *p,u32 n,char *out){const char *h="0123456789abcdef";for(u32 i=0;i<n;i++){out[i*2]=h[p[i]>>4];out[i*2+1]=h[p[i]&15];}out[n*2]=0;}
#define KU_MANIFEST 80
static inline int ku_pair_key(const char *text,u8 *out)
{
    char p[33];u32 n=0;
    while(*text){char c=*text++;if(c==' '||c=='-')continue;if(n==32)return 0;p[n++]=c;}
    p[n]=0;if(n==32)return ku_unhex(p,out,16);if(n!=26)return 0;
    u32 bits=0,value=0,j=0;
    for(u32 i=0;i<n;i++){
        char c=p[i];if(c>='a'&&c<='z')c-=32;
        int v=c>='A'&&c<='Z'?c-'A':c>='2'&&c<='7'?c-'2'+26:-1;
        if(v<0)return 0;value=(value<<5)|(u32)v;bits+=5;
        if(bits>=8){bits-=8;out[j++]=(u8)(value>>bits);}
    }
    return j==16&&!(value&3);
}
static inline void ku_pair_text(const u8 *key,char *out)
{
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";u32 bits=0,value=0,n=0,j=0;
    for(u32 i=0;i<16;i++){
        value=(value<<8)|key[i];bits+=8;
        while(bits>=5){bits-=5;if(n&&!(n%4))out[j++]='-';out[j++]=alphabet[(value>>bits)&31];n++;}
    }
    out[j++]=alphabet[(value<<(5-bits))&31];out[j]=0;
}
#define KU_MAX (255u*512u)
typedef struct {u32 sequence,size,abi;u8 digest[32];} KuRelease;
static inline int ku_manifest(const u8 *p,u32 n,const u8 *key,u32 floor,const u8 *last,u32 abi,KuRelease *r)
{
    if(n!=KU_MANIFEST||p[0]!='F'||p[1]!='X'||p[2]!='U'||p[3]!='1')return 0;
    u8 mac[32];ku_hmac(key,16,p,48,mac);if(!ku_equal(mac,p+48,32))return 0;
    u32 seq=ku_u32(p+4),size=ku_u32(p+8),target=ku_u32(p+12);
    if(!seq||seq<floor||(seq==floor&&last&&!ku_equal(last,p+16,32)))return 0;
    if(size<4096||size>KU_MAX||(size&511)||target<abi||target>65535)return 0;
    r->sequence=seq;r->size=size;r->abi=target;for(int i=0;i<32;i++)r->digest[i]=p[i+16];return 1;
}
static inline int ku_image(const u8 *p,u32 n,const KuRelease *r)
{
    if(n!=r->size||n<4096||n>KU_MAX||(n&511)||p[0]!=0xeb||(u32)(p[6]|p[7]<<8)!=n/512)return 0;
    u16 sum=0;for(u32 i=0;i<n;i+=2){u16 w=i==4?0:(u16)(p[i]|p[i+1]<<8);sum=(u16)(((sum<<1)|(sum>>15))+w);}
    if(sum!=(u16)(p[4]|p[5]<<8))return 0;
    u8 hash[32];ku_sha(p,n,hash);return ku_equal(hash,r->digest,32);
}
