#define NL_RX 2048u
#define NL_TX 4096u
#define NL_MSS 512u
enum { NL_IDLE, NL_SYN, NL_OPEN, NL_FIN };
static struct {
    u16 port, peer_port, window, mss;
    u32 ip, local_ip, serial, id, una, next, high, receive, stamp, activity;
    u32 read, received, written, sent;
    u8 mac[6], rx[NL_RX], tx[NL_TX];
    int state, tries, closing, peer_closed;
} nl;

static u32 nl_u32(const u8 *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void nl_put32(u8 *p,u32 v)
{
    p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;
}
static void nl_out(int flags,u32 seq,const u8 *data,int length)
{
    u8 packet[24+NL_MSS];
    int header=(flags&TCP_SYN)?24:20;
    memset(packet,0,(u32)header);
    packet[0]=(u8)(nl.port>>8);packet[1]=(u8)nl.port;
    packet[2]=(u8)(nl.peer_port>>8);packet[3]=(u8)nl.peer_port;
    nl_put32(packet+4,seq);nl_put32(packet+8,nl.receive);
    packet[12]=(u8)(header<<2);packet[13]=(u8)flags;
    u32 room=NL_RX-(nl.received-nl.read);
    packet[14]=(u8)(room>>8);packet[15]=(u8)room;
    if(header==24){packet[20]=2;packet[21]=4;packet[22]=2;packet[23]=0;}
    if(length)memcpy(packet+header,data,(u32)length);
    u16 sum=nw_tcp_csum(net_ip,nl.ip,packet,header+length);
    packet[16]=(u8)(sum>>8);packet[17]=(u8)sum;
    ip_send(nl.ip,nl.mac,6,packet,(u16)(header+length));
}
static void nl_drop(int reset)
{
    if(reset&&nl.state!=NL_IDLE&&net_ip==nl.local_ip)
        nl_out(TCP_RST|TCP_ACK,nl.next,0,0);
    nl.state=NL_IDLE;nl.id=0;nl.closing=nl.peer_closed=0;
    nl.read=nl.received=nl.written=nl.sent=0;
}
static void nl_segment(u32 offset,u32 n)
{
    u8 data[NL_MSS];
    for(u32 i=0;i<n;i++)data[i]=nl.tx[(nl.sent+offset+i)&(NL_TX-1)];
    nl_out(TCP_ACK|TCP_PSH,nl.una+offset,data,(int)n);
}
static void nl_pump(void)
{
    if(nl.state==NL_IDLE)return;
    if(net_ip!=nl.local_ip||(u32)(ticks-nl.activity)>60000u){nl_drop(1);return;}
    if(nl.state==NL_OPEN){
        u32 flight=nl.next-nl.una,queued=nl.written-nl.sent;int fresh=!flight;
        if(flight&&(u32)(ticks-nl.stamp)>100u){
            nl.stamp=ticks;if(++nl.tries>=8){nl_drop(1);return;}
            nl.next=nl.una;flight=0;
        }
        while(flight<queued&&flight<nl.window){
            u32 n=queued-flight;
            if(n>nl.mss)n=nl.mss;
            if(n>nl.window-flight)n=nl.window-flight;
            if(fresh){nl.stamp=ticks;nl.tries=0;fresh=0;}
            nl_segment(flight,n);flight+=n;nl.next+=n;
            if((i32)(nl.next-nl.high)>0)nl.high=nl.next;
        }
        if(flight)return;
        if(!queued&&nl.closing){
            nl.state=NL_FIN;nl.next++;nl.high=nl.next;nl.stamp=ticks;nl.tries=0;
            nl_out(TCP_FIN|TCP_ACK,nl.una,0,0);
        }else if(queued&&(u32)(ticks-nl.stamp)>100u){
            nl.stamp=ticks;nl_out(TCP_ACK,nl.next-1,0,0);
        }
        return;
    }
    if((u32)(ticks-nl.stamp)<=100u)return;
    nl.stamp=ticks;
    if(++nl.tries>=8){nl_drop(1);return;}
    if(nl.state==NL_SYN)nl_out(TCP_SYN|TCP_ACK,nl.una,0,0);
    else nl_out(TCP_FIN|TCP_ACK,nl.una,0,0);
}
static int nl_input(u32 ip,const u8 *mac,const u8 *p,int length)
{
    u16 port=(u16)(((u16)p[2]<<8)|p[3]);
    if(!nl.port||port!=nl.port)return 0;
    int header=(p[12]>>4)*4;
    if(header<20||header>length||!nc_unicast(ip))return 1;
    int flags=p[13],count=length-header;
    u16 peer=(u16)(((u16)p[0]<<8)|p[1]);
    u32 seq=nl_u32(p+4),ack=nl_u32(p+8);
    if(nl.state==NL_IDLE){
        if((flags&(TCP_SYN|TCP_ACK|TCP_RST|TCP_FIN))!=TCP_SYN)return 1;
        nl.ip=ip;nl.local_ip=net_ip;nl.peer_port=peer;memcpy(nl.mac,mac,6);
        nl.receive=seq+1;nl.una=(ticks*1103515245u)^(++nl.serial*2654435761u);
        nl.next=nl.high=nl.una+1;nl.state=NL_SYN;nl.stamp=nl.activity=ticks;
        nl.tries=0;nl.mss=NL_MSS;
        for(int i=20;i<header;){
            int option=p[i++];if(!option)break;if(option==1)continue;
            if(i>=header)break;int size=p[i++];if(size<2||size-2>header-i)break;
            if(option==2&&size==4){u16 m=(u16)(((u16)p[i]<<8)|p[i+1]);if(m&&m<nl.mss)nl.mss=m;}
            i+=size-2;
        }
        nl_out(TCP_SYN|TCP_ACK,nl.una,0,0);return 1;
    }
    if(ip!=nl.ip||peer!=nl.peer_port)return 1;
    if(flags&TCP_RST){if(seq==nl.receive)nl_drop(0);return 1;}
    if(nl.state==NL_SYN){
        if((flags&(TCP_SYN|TCP_ACK))==TCP_SYN&&seq+1==nl.receive){
            nl_out(TCP_SYN|TCP_ACK,nl.una,0,0);return 1;
        }
        if(!(flags&TCP_ACK)||(flags&TCP_SYN)||ack!=nl.next||seq!=nl.receive)return 1;
        nl.una=ack;nl.state=NL_OPEN;nl.id=nl.serial;if(!nl.id)nl.id=++nl.serial;
    }
    if((flags&TCP_SYN)||!(flags&TCP_ACK)){nl_out(TCP_ACK,nl.next,0,0);return 1;}
    u32 room=NL_RX-(nl.received-nl.read);
    if((i32)(seq-nl.receive)>0||(i32)(seq+(u32)count-nl.receive)<0||
       (i32)(ack-nl.high)>0){nl_out(TCP_ACK,nl.next,0,0);return 1;}
    nl.activity=ticks;
    if(tcp_seq_le(nl.una,ack)){
        nl.window=(u16)(((u16)p[14]<<8)|p[15]);
        if(nl.state==NL_FIN){if(ack==nl.next){nl_drop(0);return 1;}}
        else if(ack!=nl.una){
            nl.sent+=ack-nl.una;nl.una=ack;nl.tries=0;nl.stamp=ticks;
            if((i32)(nl.next-ack)<0)nl.next=ack;
        }
    }
    if(nl.state==NL_OPEN&&!nl.peer_closed){
        u32 skip=nl.receive-seq;
        int take=count-(int)skip;
        if(take>(int)room)take=(int)room;
        if(take>0){
            for(int i=0;i<take;i++)nl.rx[(nl.received+(u32)i)&(NL_RX-1)]=p[header+skip+(u32)i];
            nl.received+=(u32)take;nl.receive+=(u32)take;
        }
        if((flags&TCP_FIN)&&seq+(u32)count==nl.receive){nl.receive++;nl.peer_closed=1;}
    }
    if(count||(flags&TCP_FIN))nl_out(TCP_ACK,nl.next,0,0);
    return 1;
}
static int nl_listen(u16 port)
{
    if(!port||!net_up()||!timer_alive)return -1;
    u32 flags=net_irq_save();
    if(port==tcp_lport&&tcb.state!=TS_CLOSED&&tcb.state!=TS_TIME_WAIT){net_irq_restore(flags);return -2;}
    nl_drop(1);nl.port=port;net_irq_restore(flags);return 0;
}
static void nl_stop(void)
{
    u32 flags=net_irq_save();nl_drop(1);nl.port=0;net_irq_restore(flags);
}
static u32 nl_session(void){return nl.state==NL_OPEN&&!nl.closing?nl.id:0;}
static int nl_read(u32 session,u8 *data,int capacity)
{
    if(!data||capacity<0)return -1;
    u32 flags=net_irq_save();
    if(!session||session!=nl.id||nl.state!=NL_OPEN||nl.closing){net_irq_restore(flags);return -1;}
    u32 count=nl.received-nl.read;if(count>(u32)capacity)count=(u32)capacity;
    for(u32 i=0;i<count;i++)data[i]=nl.rx[(nl.read+i)&(NL_RX-1)];
    nl.read+=count;if(count)nl_out(TCP_ACK,nl.next,0,0);
    int result=count?(int)count:nl.peer_closed?-2:0;
    net_irq_restore(flags);return result;
}
static int nl_write(u32 session,const u8 *data,int length)
{
    if(!data||length<0)return -1;
    u32 flags=net_irq_save();
    if(!session||session!=nl.id||nl.state!=NL_OPEN||nl.closing){net_irq_restore(flags);return -1;}
    u32 count=NL_TX-(nl.written-nl.sent);if(count>(u32)length)count=(u32)length;
    for(u32 i=0;i<count;i++)nl.tx[(nl.written+i)&(NL_TX-1)]=data[i];
    nl.written+=count;net_irq_restore(flags);return (int)count;
}
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
