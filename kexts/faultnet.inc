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

static void cmd_faultnet(const char *args)
{
    char b[128],iptext[20];u32 ip=0;unsigned port=FAULTNET_PORT;
    while(*args==' ')args++;
    if(!strcmp(args,"off")){
        u32 flags=net_irq_save();
        fn_ip=0;fn_left=fn_test=0;fn_state="off";
        net_irq_restore(flags);
    }else if(!strcmp(args,"test")){
        if(!fn_ip){api->shell_print("Set a PC address first: faultnet <PC-IP> [port]\n");return;}
        fn_test=1;fn_next=ticks;
    }else if(*args){
        unsigned n=0;
        while(*args && *args!=' ' && n<sizeof iptext-1)iptext[n++]=*args++;
        iptext[n]=0;
        while(*args==' ')args++;
        if(*args){
            port=0;
            while(*args>='0' && *args<='9'){
                port=port*10+(unsigned)(*args++-'0');
                if(port>65535)break;
            }
            while(*args==' ')args++;
        }
        if(*args || !port || port>65535 || !api->net_parse_ip(iptext,&ip) ||
           !(ip&255) || (ip&255)>=224 || (ip&255)==127 || ip==net_ip ||
           (net_mask && (ip&net_mask)==(net_ip&net_mask) &&
            ((ip&~net_mask)==~net_mask || !(ip&~net_mask)))){
            api->shell_print("Usage: faultnet <PC-IP> [port] | off | test\n");return;
        }
        u32 flags=net_irq_save();
        fn_ip=ip;fn_port=(u16)port;fn_seen=fn_next=fn_sent=0;fn_left=0;fn_test=1;
        fn_session=api->rand();if(!fn_session)fn_session=1;
        fn_state="enabled";
        net_irq_restore(flags);
    }
    if(!fn_ip){api->shell_print("Network fault reports: off\n");return;}
    kfmt(b,sizeof b,"Fault reports -> %u.%u.%u.%u:%u\n",
         fn_ip&255,(fn_ip>>8)&255,(fn_ip>>16)&255,fn_ip>>24,(unsigned)fn_port);
    api->shell_print(b);
    kfmt(b,sizeof b,"%s; %u packets sent.\n",net_ip?fn_state:"waiting for network address",fn_sent);
    api->shell_print(b);
    api->shell_print("Recovered faults only. 'faultnet off' stops sending.\n");
}
