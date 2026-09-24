#pragma once
#include "update_core.inc"
#define NET_UPDATE_ABI 1u
#define KU_PUSH_PORT 7663
#define KU_HELLO_SIZE 144
#define KU_OFFER_SIZE 36
enum { KU_READY, KU_ACCEPTED, KU_DOWNLOADING, KU_VERIFYING, KU_INSTALLING,
       KU_INSTALLED, KU_FAILED, KU_CURRENT, KU_CANCELLED };
typedef struct {u32 ip,session,job,port;KuRelease release;} KuOffer;
typedef struct {
    u32 abi;
    void (*listen)(u32 checksum);
    int (*take)(KuOffer *offer);
    void (*status)(u32 state,u32 progress,const char *detail);
} NetUpdateOps;
static inline int ku_offer(const u8 *p,u32 n,u32 session,u32 abi,KuOffer *r)
{
    if(n!=KU_OFFER_SIZE||!ku_equal(p,(const u8 *)"FXP1",4)||
       ku_u32(p+4)!=session||ku_crc_feed(0,p,n-4)!=ku_u32(p+n-4))return 0;
    u32 job=ku_u32(p+8),seq=ku_u32(p+12),size=ku_u32(p+16),target=ku_u32(p+20),port=ku_u32(p+28);
    if(!job||!seq||size<4096||size>KU_MAX||(size&511)||target<abi||target>65535||!port||port>65535)return 0;
    r->session=session;r->job=job;r->port=port;
    r->release.sequence=seq;r->release.size=size;r->release.abi=target;r->release.crc=ku_u32(p+24);return 1;
}
