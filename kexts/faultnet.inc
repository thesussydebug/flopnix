#define FAULTNET_MAGIC 0x31465846u
#define FAULTNET_PORT 7661
static u32 fn_ip,fn_session,fn_seen,fn_next,fn_sent;
static u16 fn_port=FAULTNET_PORT;
static int fn_left,fn_test;
static FaultRec fn_record;
static const char *fn_state="off";

static void faultnet_poll(void)
{
    if(!fn_ip || !net_up() || !net_ip)return;
    if((i32)(ticks-fn_next)<0)return;
    fn_next=ticks+50;
    if(!fn_left){
        if(fn_test){
            memset(&fn_record,0,sizeof fn_record);
            fn_record.vec=0xfffffffeu;fn_record.tick=ticks;
            strlcpy(fn_record.location,"Connection test",sizeof fn_record.location);
            fn_test=0;fn_left=3;
        }else for(int i=api->fault_count()-1;i>=0;i--){
            const FaultRec *r=api->fault_get(i);
            if(r && (i32)(r->sequence-fn_seen)>0){
                memcpy(&fn_record,r,sizeof fn_record);fn_left=3;break;
            }
        }
    }
    if(!fn_left){fn_state="waiting for faults";return;}
    u32 hop=(fn_ip&net_mask)==(net_ip&net_mask)?fn_ip:net_gw;
    if(!hop){fn_state="no route to PC";return;}
    u8 mac[6];
    if(!arp_lookup(hop,mac)){
        static const u8 zero[6]={0};
        arp_send(1,zero,hop);fn_state="waiting for PC/router";return;
    }
    struct {u32 magic,session;FaultRec fault;} packet;
    _Static_assert(sizeof packet==176,"fault report wire layout");
    packet.magic=FAULTNET_MAGIC;packet.session=fn_session;
    memcpy(&packet.fault,&fn_record,sizeof fn_record);
    udp_send(fn_ip,mac,FAULTNET_PORT,fn_port,(const u8 *)&packet,sizeof packet);
    fn_sent++;fn_state="sent (UDP, no acknowledgement)";
    if(!--fn_left && fn_record.sequence)fn_seen=fn_record.sequence;
}
