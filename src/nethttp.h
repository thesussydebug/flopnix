#pragma once
#define NET_HTTP_ABI 1u
typedef struct {
    int status;
    u32 length;
    u8 length_known;
    char type[80],location[256],filename[64],encoding[24];
} NetHttpInfo;
typedef struct {
    u32 abi;
    int (*get)(u32 ip,u16 port,const char *host,const char *path,
               int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout,NetHttpInfo *info);
} NetHttpOps;
