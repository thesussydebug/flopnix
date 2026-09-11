/* Checks archive headers and file entries. */
#pragma once
#include "lz.inc"
#define AR_FILES 32
#define AR_CAP 131072u
#define AR_FILE 65536u
typedef struct { char name[24]; u32 offset, packed, raw; } ArEntry;
static int ar_name(const char *s)
{
    int n = 0;
    for (; n < 24 && s[n]; n++) {
        u8 c = (u8)s[n];
        if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
    }
    return n && n < 24 && s[0] != '.' && s[n-1] != ' ' && s[n-1] != '.';
}
static void ar_empty(u8 *d)
{
    d[0]='F'; d[1]='P'; d[2]='A'; d[3]='1';
    for (int i=4; i<8; i++) d[i]=0;
}
static int ar_same(const char *a, const char *b)
{
    for (int i=0; ; i++) {
        int x=a[i], y=b[i];
        if (x>='A'&&x<='Z') x+=32;
        if (y>='A'&&y<='Z') y+=32;
        if (x!=y) return 0;
        if (!x) return 1;
    }
}

static int ar_index(const u8 *d, u32 n, ArEntry *e)
{
    if (n<8 || n>AR_CAP || d[0]!='F'||d[1]!='P'||d[2]!='A'||d[3]!='1' || d[4]>AR_FILES || d[5]||d[6]||d[7]) return -1;
    u32 off=8;
    for (int i=0; i<d[4]; i++) {
        if (n-off<28) return -1;
        for (int j=0; j<24; j++) e[i].name[j]=(char)d[off+j];
        if (!ar_name(e[i].name)) return -1;
        for (int j=0; j<i; j++) if (ar_same(e[j].name,e[i].name)) return -1;
        u32 len=lz_get32(d+off+24); off+=28;
        if (len<LZ_HDR || len>n-off || d[off]!='P'||d[off+1]!='Z'||d[off+2]!='1'||d[off+3] || d[off+10]>1 || d[off+11]) return -1;
        u32 raw=lz_get32(d+off+4);
        if (raw>AR_FILE || (d[off+10] && len != LZ_HDR+raw)) return -1;
        e[i].offset=off; e[i].packed=len; e[i].raw=raw; off+=len;
    }
    return off==n ? d[4] : -1;
}
