#include "debugnet.h"
#define DN_PORT 7665
#define DN_MAGIC 0x31445846u
static u32 dn_nonce,dn_next,dn_peer;
static u16 dn_port;
static u8 dn_mac[6];
static int dn_auto,dn_search;
typedef struct {u32 magic,nonce,op,port,crc;} DnPacket;
_Static_assert(sizeof(DnPacket)==20,"debug discovery layout");

static void dn_stop(void)
{
    dn_search=0;dn_peer=dn_nonce=0;
}

static void dn_start(void)
{
    pm_disable();dn_peer=0;dn_search=1;dn_next=ticks;
    dn_nonce=api->rand();if(!dn_nonce)dn_nonce=1;
}

static void dn_set_auto(int enabled)
{
    u32 flags=net_irq_save();dn_auto=!!enabled;
    if(dn_auto)dn_start();else{dn_stop();pm_disable();}
    net_irq_restore(flags);
}

static int dn_get_auto(void){return dn_auto;}
static const NetDebugOps dn_ops={NET_DEBUG_ABI,dn_set_auto,dn_get_auto};

static void dn_receive(u32 ip,const u8 *mac,const u8 *data,u32 size)
{
    if(!dn_search || dn_peer || !net_ip || !net_mask || size!=sizeof(DnPacket) ||
       !nc_host_valid(ip,net_mask) || ip==net_ip ||
       (ip&net_mask)!=(net_ip&net_mask) || (mac[0]&1))return;
    DnPacket p;memcpy(&p,data,sizeof p);
    if(p.magic!=DN_MAGIC || p.nonce!=dn_nonce || p.op!=1 || p.port<2 ||
       p.port>65535 || pm_crc(&p,sizeof p-4)!=p.crc)return;
    dn_peer=ip;dn_port=(u16)p.port;memcpy(dn_mac,mac,6);
}

static void dn_poll(void)
{
    if(!dn_search || !net_up() || !net_ip || !net_mask || !timer_alive)return;
    if(dn_peer){
        arp_store(dn_peer,dn_mac);
        pm_enable(dn_peer,dn_port);dn_stop();return;
    }
    if((i32)(ticks-dn_next)<0)return;
    dn_next=ticks+200;
    DnPacket p={DN_MAGIC,dn_nonce,0,0,0};p.crc=pm_crc(&p,sizeof p-4);
    static const u8 broadcast[6]={255,255,255,255,255,255};
    udp_send(net_ip|~net_mask,broadcast,DN_PORT,DN_PORT,(const u8 *)&p,sizeof p);
}
