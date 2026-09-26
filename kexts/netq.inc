/* Receive queue, DNS cache and TCP sizing helpers for the network stack. */
#pragma once
#define RXQ_N 32
#define RXQ_FRAME 1536
#define DNS_CACHE 8

typedef struct {u8 *buf;u16 len[RXQ_N];volatile u32 head,tail;u32 drops,peak;} RxQueue;
static void rxq_init(RxQueue *q,u8 *storage){q->buf=storage;q->head=q->tail=0;q->drops=q->peak=0;}
static u8 *rxq_slot(RxQueue *q)
{
    if(!q->buf||q->tail-q->head>=RXQ_N){q->drops++;return 0;}
    return q->buf+(q->tail%RXQ_N)*RXQ_FRAME;
}
static void rxq_commit(RxQueue *q,u16 len)
{
    q->len[q->tail%RXQ_N]=len;__asm__ volatile("" ::: "memory");q->tail++;
    if(q->tail-q->head>q->peak)q->peak=q->tail-q->head;
}
static u8 *rxq_peek(RxQueue *q,u16 *len)
{
    if(q->tail==q->head)return 0;u32 i=q->head%RXQ_N;*len=q->len[i];return q->buf+i*RXQ_FRAME;
}
static void rxq_pop(RxQueue *q){__asm__ volatile("" ::: "memory");q->head++;}

typedef struct {char name[64];u32 ip,expires;} DnsEntry;
typedef struct {DnsEntry e[DNS_CACHE];int next;} DnsCache;
static void dns_cache_init(DnsCache *c){for(int i=0;i<DNS_CACHE;i++){c->e[i].name[0]=0;c->e[i].ip=0;}c->next=0;}
static int dns_name_eq(const char *a,const char *b)
{
    for(;;a++,b++){int x=*a,y=*b;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;if(!x)return 1;}
}
static u32 dns_cache_get(const DnsCache *c,const char *name,u32 now)
{
    for(int i=0;i<DNS_CACHE;i++)if(c->e[i].ip&&(i32)(c->e[i].expires-now)>0&&dns_name_eq(c->e[i].name,name))return c->e[i].ip;
    return 0;
}
static void dns_cache_put(DnsCache *c,const char *name,u32 ip,u32 now,u32 ttl)
{
    int n=0;while(name[n])n++;if(!ip||!n||n>=64)return;
    int slot=-1;for(int i=0;i<DNS_CACHE;i++)if(c->e[i].ip&&dns_name_eq(c->e[i].name,name))slot=i;
    if(slot<0){slot=c->next;c->next=(c->next+1)%DNS_CACHE;}
    for(int i=0;i<=n;i++)c->e[slot].name[i]=name[i];c->e[slot].ip=ip;c->e[slot].expires=now+ttl;
}

static u32 tcp_rx_window(u32 queue_free,u32 slots,u32 mss)
{
    u32 cap=slots>3?(slots-2)*mss:mss,w=queue_free<cap?queue_free:cap;
    return w>65535?65535:w;
}
static u32 tcp_mss_for_mtu(u32 mtu){return mtu<576?536:mtu>1500?1460:mtu-40;}
