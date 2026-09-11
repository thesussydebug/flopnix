#pragma once

#define NET_TEXT_ABI 1u
typedef struct {
    u32 abi;
    int (*request)(u32 ip,u16 port,const char *request,
                   int (*sink)(const u8 *,int,void *),void *ctx,u32 timeout);
} NetTextOps;
