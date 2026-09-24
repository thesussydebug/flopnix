#include "netupdate.h"
static struct {
    u8 magic[4];u32 session,job,state,progress,checksum,kernel,abi;
    u8 mac[6];char version[26],detail[76];u32 crc;
} push_hello;
_Static_assert(sizeof push_hello==KU_HELLO_SIZE,"update discovery layout");
static KuOffer push_offer;
static u32 push_peer,push_next,push_seen[8],push_seen_next;
static u8 push_mac[6];
static int push_enabled,push_pending;
static void push_send(u32 ip,const u8 *mac)
{
    push_hello.crc=ku_crc_feed(0,(const u8 *)&push_hello,sizeof push_hello-4);
    udp_send(ip,mac,KU_PUSH_PORT,KU_PUSH_PORT,(const u8 *)&push_hello,sizeof push_hello);
}
static void push_status(u32 state,u32 progress,const char *detail)
{
    u32 f=net_irq_save();
    push_hello.state=state;push_hello.progress=progress>256?256:progress;
    strlcpy(push_hello.detail,detail,sizeof push_hello.detail);
    if(push_peer&&net_ip)push_send(push_peer,push_mac);
    push_next=ticks;
    net_irq_restore(f);
}
static void push_listen(u32 checksum)
{
    if(push_enabled)return;
    memcpy(push_hello.magic,"FXH1",4);push_hello.session=api->rand()^ticks^checksum^rtc_now_dos();
    if(!push_hello.session)push_hello.session=1;
    push_hello.checksum=checksum;push_hello.kernel=*(const u32 *)0x8004;
    push_hello.abi=api->version;memcpy(push_hello.mac,net_mac,6);
    strlcpy(push_hello.version,api->os_version,sizeof push_hello.version);
    push_enabled=1;push_next=ticks;
}
static int push_take(KuOffer *offer)
{
    u32 f=net_irq_save();int ready=push_pending&&!tcp_busy;
    if(ready){memcpy(offer,&push_offer,sizeof *offer);push_pending=0;}
    net_irq_restore(f);return ready;
}
static void push_receive(u32 ip,const u8 *mac,const u8 *data,u32 n)
{
    if(!push_enabled||!net_ip||!nc_unicast(ip)||ip==net_ip||
       (ip&net_mask)!=(net_ip&net_mask))return;
    KuOffer offer;
    if(!ku_offer(data,n,push_hello.session,api->version,&offer))return;
    for(u32 i=0;i<8;i++)if(push_seen[i]==offer.job)return;
    if(push_pending||
       (push_hello.state>=KU_ACCEPTED&&push_hello.state<=KU_INSTALLING))return;
    push_seen[push_seen_next++%8]=offer.job;
    offer.ip=ip;push_offer=offer;push_peer=ip;memcpy(push_mac,mac,6);
    push_hello.job=offer.job;push_pending=1;
    push_status(KU_ACCEPTED,0,"Waiting for the network");
}
static void push_poll(void)
{
    if(!push_enabled||!net_ip||!timer_alive||(i32)(ticks-push_next)<0)return;
    push_next=ticks+100;
    static const u8 broadcast[6]={255,255,255,255,255,255},zero[6]={0};
    push_send(net_ip|~net_mask,broadcast);
    if(push_peer)push_send(push_peer,push_mac);
    if(net_gw&&net_gw!=push_peer){
        u8 mac[6];if(arp_lookup(net_gw,mac))push_send(net_gw,mac);else arp_send(1,zero,net_gw);
    }
}
static const NetUpdateOps push_ops={NET_UPDATE_ABI,push_listen,push_take,push_status};
