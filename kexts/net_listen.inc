#define NL_RX 2048u
#define NL_TX 4096u
#define NL_MSS 512u
enum { NL_IDLE, NL_SYN, NL_OPEN, NL_FIN, NL_DIAL };
typedef struct {
    u16 port, peer_port, window, mss;
    u32 ip, local_ip, serial, id, una, next, high, receive, stamp, activity;
    u32 read, received, written, sent;
    u8 mac[6], rx[NL_RX], tx[NL_TX];
    int state, tries, closing, peer_closed, dial;
} NlConn;
static NlConn nl;
static NlConn *ns;

static u32 nl_u32(const u8 *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void nl_put32(u8 *p,u32 v)
{
    p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;
}
static void nl_out(NlConn *c,int flags,u32 seq,const u8 *data,int length)
{
    u8 packet[24+NL_MSS];
    int header=(flags&TCP_SYN)?24:20;
    memset(packet,0,(u32)header);
    packet[0]=(u8)(c->port>>8);packet[1]=(u8)c->port;
    packet[2]=(u8)(c->peer_port>>8);packet[3]=(u8)c->peer_port;
    nl_put32(packet+4,seq);nl_put32(packet+8,c->receive);
    packet[12]=(u8)(header<<2);packet[13]=(u8)flags;
    u32 room=NL_RX-(c->received-c->read);
    packet[14]=(u8)(room>>8);packet[15]=(u8)room;
    if(header==24){packet[20]=2;packet[21]=4;packet[22]=2;packet[23]=0;}
    if(length)memcpy(packet+header,data,(u32)length);
    u16 sum=nw_tcp_csum(net_ip,c->ip,packet,header+length);
    packet[16]=(u8)(sum>>8);packet[17]=(u8)sum;
    ip_send(c->ip,c->mac,6,packet,(u16)(header+length));
}
static void nl_drop(NlConn *c,int reset)
{
    if(reset&&c->state!=NL_IDLE&&c->state!=NL_DIAL&&net_ip==c->local_ip)
        nl_out(c,TCP_RST|TCP_ACK,c->next,0,0);
    c->state=NL_IDLE;c->id=0;c->closing=c->peer_closed=0;
    c->read=c->received=c->written=c->sent=0;
}
static void nl_segment(NlConn *c,u32 offset,u32 n)
{
    u8 data[NL_MSS];
    for(u32 i=0;i<n;i++)data[i]=c->tx[(c->sent+offset+i)&(NL_TX-1)];
    nl_out(c,TCP_ACK|TCP_PSH,c->una+offset,data,(int)n);
}
static void nl_pump1(NlConn *c)
{
    if(c->state==NL_IDLE)return;
    if(net_ip!=c->local_ip||(!c->dial&&(u32)(ticks-c->activity)>60000u)){nl_drop(c,1);return;}
    if(c->state==NL_OPEN){
        u32 flight=c->next-c->una,queued=c->written-c->sent;int fresh=!flight;
        if(flight&&(u32)(ticks-c->stamp)>100u){
            c->stamp=ticks;if(++c->tries>=8){nl_drop(c,1);return;}
            c->next=c->una;flight=0;
        }
        while(flight<queued&&flight<c->window){
            u32 n=queued-flight;
            if(n>c->mss)n=c->mss;
            if(n>c->window-flight)n=c->window-flight;
            if(fresh){c->stamp=ticks;c->tries=0;fresh=0;}
            nl_segment(c,flight,n);flight+=n;c->next+=n;
            if((i32)(c->next-c->high)>0)c->high=c->next;
        }
        if(flight)return;
        if(!queued&&c->closing){
            c->state=NL_FIN;c->next++;c->high=c->next;c->stamp=ticks;c->tries=0;
            nl_out(c,TCP_FIN|TCP_ACK,c->una,0,0);
        }else if(queued&&(u32)(ticks-c->stamp)>100u){
            c->stamp=ticks;nl_out(c,TCP_ACK,c->next-1,0,0);
        }
        return;
    }
    if((u32)(ticks-c->stamp)<=100u)return;
    c->stamp=ticks;
    if(++c->tries>=8){nl_drop(c,1);return;}
    if(c->state==NL_SYN)nl_out(c,TCP_SYN|TCP_ACK,c->una,0,0);
    else if(c->state==NL_DIAL)nl_out(c,TCP_SYN,c->una,0,0);
    else nl_out(c,TCP_FIN|TCP_ACK,c->una,0,0);
}
static void nl_pump(void)
{
    nl_pump1(&nl);
    if(ns)nl_pump1(ns);
}
static void nl_mss(NlConn *c,const u8 *p,int header)
{
    c->mss=NL_MSS;
    for(int i=20;i<header;){
        int option=p[i++];if(!option)break;if(option==1)continue;
        if(i>=header)break;int size=p[i++];if(size<2||size-2>header-i)break;
        if(option==2&&size==4){u16 m=(u16)(((u16)p[i]<<8)|p[i+1]);if(m&&m<c->mss)c->mss=m;}
        i+=size-2;
    }
}
static int nl_input1(NlConn *c,u32 ip,const u8 *mac,const u8 *p,int length)
{
    u16 port=(u16)(((u16)p[2]<<8)|p[3]);
    if(!c->port||port!=c->port)return 0;
    int header=(p[12]>>4)*4;
    if(header<20||header>length||!nc_unicast(ip))return 1;
    int flags=p[13],count=length-header;
    u16 peer=(u16)(((u16)p[0]<<8)|p[1]);
    u32 seq=nl_u32(p+4),ack=nl_u32(p+8);
    if(c->state==NL_IDLE){
        if(c->dial||(flags&(TCP_SYN|TCP_ACK|TCP_RST|TCP_FIN))!=TCP_SYN)return 1;
        c->ip=ip;c->local_ip=net_ip;c->peer_port=peer;memcpy(c->mac,mac,6);
        c->receive=seq+1;c->una=(ticks*1103515245u)^(++c->serial*2654435761u);
        c->next=c->high=c->una+1;c->state=NL_SYN;c->stamp=c->activity=ticks;
        c->tries=0;nl_mss(c,p,header);
        nl_out(c,TCP_SYN|TCP_ACK,c->una,0,0);return 1;
    }
    if(ip!=c->ip||peer!=c->peer_port)return 1;
    if(c->state==NL_DIAL){
        if(flags&TCP_RST){if((flags&TCP_ACK)&&ack==c->next)nl_drop(c,0);return 1;}
        if((flags&(TCP_SYN|TCP_ACK))!=(TCP_SYN|TCP_ACK)||ack!=c->next)return 1;
        c->receive=seq+1;c->una=ack;c->state=NL_OPEN;c->id=++c->serial;
        c->window=(u16)(((u16)p[14]<<8)|p[15]);c->activity=c->stamp=ticks;c->tries=0;
        nl_mss(c,p,header);
        nl_out(c,TCP_ACK,c->next,0,0);return 1;
    }
    if(flags&TCP_RST){if(seq==c->receive)nl_drop(c,0);return 1;}
    if(c->state==NL_SYN){
        if((flags&(TCP_SYN|TCP_ACK))==TCP_SYN&&seq+1==c->receive){
            nl_out(c,TCP_SYN|TCP_ACK,c->una,0,0);return 1;
        }
        if(!(flags&TCP_ACK)||(flags&TCP_SYN)||ack!=c->next||seq!=c->receive)return 1;
        c->una=ack;c->state=NL_OPEN;c->id=c->serial;if(!c->id)c->id=++c->serial;
    }
    if((flags&TCP_SYN)||!(flags&TCP_ACK)){nl_out(c,TCP_ACK,c->next,0,0);return 1;}
    u32 room=NL_RX-(c->received-c->read);
    if((i32)(seq-c->receive)>0||(i32)(seq+(u32)count-c->receive)<0||
       (i32)(ack-c->high)>0){nl_out(c,TCP_ACK,c->next,0,0);return 1;}
    c->activity=ticks;
    if(tcp_seq_le(c->una,ack)){
        c->window=(u16)(((u16)p[14]<<8)|p[15]);
        if(c->state==NL_FIN){if(ack==c->next){nl_drop(c,0);return 1;}}
        else if(ack!=c->una){
            c->sent+=ack-c->una;c->una=ack;c->tries=0;c->stamp=ticks;
            if((i32)(c->next-ack)<0)c->next=ack;
        }
    }
    if(c->state==NL_OPEN&&!c->peer_closed){
        u32 skip=c->receive-seq;
        int take=count-(int)skip;
        if(take>(int)room)take=(int)room;
        if(take>0){
            for(int i=0;i<take;i++)c->rx[(c->received+(u32)i)&(NL_RX-1)]=p[header+skip+(u32)i];
            c->received+=(u32)take;c->receive+=(u32)take;
        }
        if((flags&TCP_FIN)&&seq+(u32)count==c->receive){c->receive++;c->peer_closed=1;}
    }
    if(count||(flags&TCP_FIN))nl_out(c,TCP_ACK,c->next,0,0);
    return 1;
}
static int nl_input(u32 ip,const u8 *mac,const u8 *p,int length)
{
    return nl_input1(&nl,ip,mac,p,length)||(ns&&nl_input1(ns,ip,mac,p,length));
}
static int nl_take(NlConn *c,u32 session,u8 *data,int capacity)
{
    if(!data||capacity<0)return -1;
    u32 flags=net_irq_save();
    if(!c||!session||session!=c->id||c->state!=NL_OPEN||c->closing){net_irq_restore(flags);return -1;}
    u32 count=c->received-c->read;if(count>(u32)capacity)count=(u32)capacity;
    for(u32 i=0;i<count;i++)data[i]=c->rx[(c->read+i)&(NL_RX-1)];
    c->read+=count;if(count)nl_out(c,TCP_ACK,c->next,0,0);
    int result=count?(int)count:c->peer_closed?-2:0;
    net_irq_restore(flags);return result;
}
static int nl_give(NlConn *c,u32 session,const u8 *data,int length)
{
    if(!data||length<0)return -1;
    u32 flags=net_irq_save();
    if(!c||!session||session!=c->id||c->state!=NL_OPEN||c->closing){net_irq_restore(flags);return -1;}
    u32 count=NL_TX-(c->written-c->sent);if(count>(u32)length)count=(u32)length;
    for(u32 i=0;i<count;i++)c->tx[(c->written+i)&(NL_TX-1)]=data[i];
    c->written+=count;net_irq_restore(flags);return (int)count;
}
static int nl_listen(u16 port)
{
    if(!port||!net_up()||!timer_alive)return -1;
    u32 flags=net_irq_save();
    if(port==tcp_lport&&tcb.state!=TS_CLOSED&&tcb.state!=TS_TIME_WAIT){net_irq_restore(flags);return -2;}
    nl_drop(&nl,1);nl.port=port;net_irq_restore(flags);return 0;
}
static void nl_stop(void)
{
    u32 flags=net_irq_save();nl_drop(&nl,1);nl.port=0;net_irq_restore(flags);
}
static u32 nl_session(void){return nl.state==NL_OPEN&&!nl.closing?nl.id:0;}
static int nl_read(u32 session,u8 *data,int capacity){return nl_take(&nl,session,data,capacity);}
static int nl_write(u32 session,const u8 *data,int length){return nl_give(&nl,session,data,length);}
static void nl_close(u32 session)
{
    u32 flags=net_irq_save();if(session&&session==nl.id)nl.closing=1;net_irq_restore(flags);
}
static int nl_pending(u32 session)
{
    if(!session||session!=nl.id||nl.state==NL_IDLE)return 0;
    return nl.state==NL_FIN?1:(int)(nl.written-nl.sent);
}
static const NetListenOps listen_ops={NET_LISTEN_ABI,nl_listen,nl_stop,nl_session,nl_read,nl_write,nl_close,net_poll,nl_pending};

static void ns_close(void)
{
    u32 flags=net_irq_save();
    NlConn *c=ns;
    if(c&&c->state==NL_OPEN)c->closing=1;
    net_irq_restore(flags);
    if(!c)return;
    u32 start=ticks;
    while((c->state==NL_OPEN||c->state==NL_FIN)&&(u32)(ticks-start)<200u)net_wait();
    flags=net_irq_save();nl_drop(c,1);ns=0;net_irq_restore(flags);
    api->kfree(c);
}
static int ns_open(u32 ip,u16 port,u32 timeout)
{
    if(!ip||!port||!net_up()||!timer_alive)return -1;
    if(ns)return -2;
    NlConn *c=api->kmalloc(sizeof *c);
    if(!c)return -3;
    memset(c,0,sizeof *c);
    u32 hop=((ip&net_mask)==(net_ip&net_mask))?ip:net_gw;
    api->network_lock();
    int found=arp_resolve(hop,c->mac,timeout);
    api->network_unlock();
    if(!found){api->kfree(c);return -1;}
    u32 flags=net_irq_save();
    if(ns){net_irq_restore(flags);api->kfree(c);return -2;}
    c->dial=1;c->ip=ip;c->local_ip=net_ip;c->peer_port=port;
    c->port=(u16)(0x8000|(ticks&0x3FFF));
    if(c->port==nl.port)c->port^=0x2000;
    c->una=(ticks*1103515245u)^0x54454C4Eu;c->next=c->high=c->una+1;
    c->state=NL_DIAL;c->stamp=c->activity=ticks;
    ns=c;
    nl_out(c,TCP_SYN,c->una,0,0);
    net_irq_restore(flags);
    u32 start=ticks;
    net_cancel=0;api->esc_arm();
    while(c->state==NL_DIAL&&!net_cancel&&(u32)(ticks-start)<timeout)net_wait();
    if(c->state==NL_OPEN)return 0;
    ns_close();
    return -1;
}
static int ns_read(u8 *data,int capacity){return ns?nl_take(ns,ns->id,data,capacity):-1;}
static int ns_write(const u8 *data,int length){return ns?nl_give(ns,ns->id,data,length):-1;}
static const NetStreamOps stream_ops={NET_STREAM_ABI,ns_open,ns_read,ns_write,ns_close,net_poll};
