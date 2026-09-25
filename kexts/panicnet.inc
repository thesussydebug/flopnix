#include "panicnet.h"

#define PM_PORT 7662
#define PM_MAGIC 0x324d5046u
#define PM_BYTES 512u
static const PanicCore *pm_core;
static u32 pm_ip,pm_session,pm_ram_end,pm_hop,pm_extra[8];
static u16 pm_port=PM_PORT;
static u8 pm_mac[6];
static int pm_mac_ready,pm_captured;
static PanicCpu pm_cpu,pm_original;
static u8 pm_stack[8192] __attribute__((aligned(16),used));
static u32 pm_pd[1024] __attribute__((aligned(4096)));
typedef struct {u32 magic,session,op,seq,address,length,status,crc;} PmHeader;
static struct {PmHeader header;u8 data[PM_BYTES];} pm_reply;
typedef struct {char name[24];u32 base,size,id,private_base,private_size;int status;u32 kind;} PmModule;
static PmModule pm_modules[FS_NFILES];
static u32 pm_system[24];
static char pm_log[2048],pm_trace[4096],pm_debug[2048];
static FaultRec pm_faults[12];
static u8 pm_threads[32*16];
static MemBuffer pm_buffers[32];
static u32 pm_next_cache;

static u32 pm_crc(const void *data,u32 length)
{
    const u8 *p=data;u32 crc=~0u;
    while(length--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
    return ~crc;
}

static void pm_text(char *out,const char *in,u32 cap)
{
    if(!cap)return;
    u32 n=0;while(n+1<cap && in[n]){out[n]=in[n];n++;}out[n]=0;
}

static int pm_ram(u32 at,u32 length)
{
    if(!length || at<4096 || at>=pm_ram_end || length>pm_ram_end-at)return 0;
    return at>=0x100000 || (at<0xa0000 && length<=0xa0000-at);
}

static int pm_translate(u32 va,u32 *physical)
{
    u32 pa=va;
    if(pm_cpu.cr0&0x80000000u){
        u32 table=pm_cpu.cr3&~4095u;
        if(!pm_ram(table,4096))return 0;
        u32 pde=((const u32 *)table)[va>>22];
        if(!(pde&1))return 0;
        if(pde&128){
            if(!(pm_cpu.cr4&16) || (pde&0x003fe000u))return 0;
            pa=(pde&0xffc00000u)|(va&0x3fffffu);
        }else{
            table=pde&~4095u;
            if(!pm_ram(table,4096))return 0;
            u32 pte=((const u32 *)table)[(va>>12)&1023];
            if(!(pte&1))return 0;
            pa=(pte&~4095u)|(va&4095);
        }
    }
    if(!pm_ram(pa,1))return 0;
    *physical=pa;return 1;
}

static int pm_read(u32 at,u8 *out,u32 length)
{
    if(!length || length>PM_BYTES || at>0xffffffffu-(length-1))return 0;
    for(u32 n=0;n<length;){
        u32 pa;
        if(!pm_translate(at+n,&pa))return 0;
        u32 chunk=4096-((at+n)&4095);
        if(chunk>length-n)chunk=length-n;
        if(!pm_ram(pa,chunk))return 0;
        memcpy(out+n,(const void *)pa,chunk);n+=chunk;
    }
    return 1;
}


static void pm_snapshot(void)
{
    const u32 *m=pm_core->layout;
    pm_system[0]=2;pm_system[1]=ticks;pm_system[2]=pm_ram_end;
    pm_system[3]=*(const u32 *)0x8004;pm_system[4]=pm_core->kernel_end;
    pm_system[5]=*pm_core->fault_count;
    pm_system[6]=m[6];pm_system[7]=*pm_core->heap_top;
    pm_system[8]=*pm_core->grow_base;pm_system[9]=*pm_core->grow_top;
    pm_system[10]=m[2];pm_system[11]=m[3];pm_system[12]=*pm_core->arena_rw;
    pm_system[13]=m[4];pm_system[14]=m[5];pm_system[15]=m[0];pm_system[16]=m[1];
    pm_system[17]=nic_kind;pm_system[18]=net_ip;pm_system[19]=net_mask;
    pm_system[20]=net_gw;pm_system[21]=diag_rx;pm_system[22]=diag_tx;
    int count=*pm_core->module_count;
    if(count<0 || count>FS_NFILES){count=0;pm_extra[7]|=1;}
    pm_system[23]=(u32)count;
    for(int i=0;i<count;i++){
        const KextInfo *k=&pm_core->modules[i];PmModule *d=&pm_modules[i];
        pm_text(d->name,k->name,sizeof d->name);
        d->base=k->base;d->size=k->size;d->id=pm_core->module_ids[i];
        d->private_base=pm_core->private_base[i];d->private_size=pm_core->private_size[i];
        d->status=k->status;d->kind=k->kind;
    }
    pm_text(pm_log,pm_core->log,sizeof pm_log);
    pm_text(pm_trace,pm_core->trace,sizeof pm_trace);
    memcpy(pm_faults,pm_core->faults,sizeof pm_faults);
    u32 n=pm_core->thread_bytes;if(n>sizeof pm_threads){n=sizeof pm_threads;pm_extra[7]|=2;}
    memset(pm_threads,0,sizeof pm_threads);memcpy(pm_threads,pm_core->threads,n);
    memcpy(pm_buffers,pm_core->buffers,sizeof pm_buffers);
}

static void pm_fault(FaultRec *r,u32 address)
{
    memset(r,0,sizeof *r);r->vec=pm_cpu.vec;r->err=pm_cpu.err;r->eip=address;
    r->cr2=pm_cpu.cr2;r->tick=pm_system[1];
    for(u32 i=0;i<pm_system[23];i++){
        const PmModule *m=&pm_modules[i];
        if(address>=m->base && address-m->base<m->size){
            pm_text(r->module,m->name,sizeof r->module);r->module_base=m->base;r->module_id=m->id;break;
        }
    }
    if(!r->module[0] && address>=0x8000 && address-0x8000<(*(const u16 *)0x8006)*512u){
        pm_text(r->module,"kernel",sizeof r->module);r->module_base=0x8000;r->module_id=pm_system[3];
    }
    pm_text(r->location,r->module[0]?r->module:"Unknown location",sizeof r->location);
    pm_text(r->owner,r->module,sizeof r->owner);
}

static void pm_send(u32 op,u32 seq,u32 address,u32 length,u32 status)
{
    PmHeader *h=&pm_reply.header;
    h->magic=PM_MAGIC;h->session=pm_session;h->op=op;h->seq=seq;
    h->address=address;h->length=length;h->status=status;h->crc=0;
    h->crc=pm_crc(&pm_reply,sizeof *h+length);
    udp_send(pm_ip,pm_mac,PM_PORT,pm_port,(const u8 *)&pm_reply,(u16)(sizeof *h+length));
}

static void pm_info(u32 seq)
{
    _Static_assert(sizeof(PanicCpu)==72,"panic register layout");
    _Static_assert(sizeof(PmHeader)==32,"panic packet layout");
    _Static_assert(sizeof(PmModule)==52,"panic module layout");
    memcpy(pm_reply.data,&pm_cpu,sizeof pm_cpu);
    pm_fault((FaultRec *)(pm_reply.data+72),pm_cpu.eip);
    memcpy(pm_reply.data+240,&pm_ram_end,4);
    memcpy(pm_reply.data+244,pm_extra,32);
    memcpy(pm_reply.data+276,&pm_original,72);
    pm_send(0,seq,0,348,0);
}

static void pm_blob(const PmHeader *q)
{
    const void *src=0;u32 size=0,channel=q->address>>24,offset=q->address&0xffffff;
    switch(channel){
    case 1:src=pm_log;size=sizeof pm_log;break;
    case 2:src=pm_trace;size=sizeof pm_trace;break;
    case 3:src=pm_faults;size=sizeof pm_faults;break;
    case 4:src=pm_modules;size=pm_system[23]*sizeof(PmModule);break;
    case 5:src=pm_system;size=sizeof pm_system;break;
    case 6:src=pm_debug;size=sizeof pm_debug;break;
    case 7:src=pm_threads;size=pm_core->thread_bytes;if(size>sizeof pm_threads)size=sizeof pm_threads;break;
    case 8:src=pm_buffers;size=sizeof pm_buffers;break;
    }
    if(!src || !q->length || q->length>PM_BYTES){pm_send(q->op,q->seq,q->address,0,2);return;}
    u32 n=offset<size?size-offset:0;if(n>q->length)n=q->length;
    if(n)memcpy(pm_reply.data,(const u8 *)src+offset,n);
    pm_send(q->op,q->seq,q->address,n,0);
}

static void pm_region(const PmHeader *q)
{
    u32 index=q->address,base=0,size=0,kind=index+1;
    switch(index){
    case 0:base=0x7000;size=0x1000;break;
    case 1:base=0x8000;size=(*(const u16 *)0x8006)*512u;break;
    case 2:base=0x100000;size=pm_system[4]-base;break;
    case 3:base=0x80000;size=0x1e000;break;
    case 4:base=pm_system[15];size=pm_system[16]-base;break;
    case 5:base=0x270000;size=0x144000;break;
    case 6:base=pm_system[12];size=pm_system[11]-base;break;
    default:
        index-=7;
        if(index/2>=pm_system[23]){pm_send(q->op,q->seq,q->address,0,0);return;}
        const PmModule *m=&pm_modules[index/2];kind=8+(index&1);
        base=(index&1)?m->private_base:m->base;size=(index&1)?m->private_size:m->size;
        break;
    }
    if(size && !pm_ram(base,size)){pm_send(q->op,q->seq,q->address,0,3);return;}
    u32 result[]={base,size,kind};memcpy(pm_reply.data,result,sizeof result);
    pm_send(q->op,q->seq,q->address,sizeof result,0);
}

static int pm_heap_range(u32 at,u32 size)
{
    return pm_ram(at,size) && !(at&7) &&
        ((at>=pm_system[6] && at<=pm_system[7] && size<=pm_system[7]-at) ||
         (at>=pm_system[8] && at<=pm_system[9] && size<=pm_system[9]-at));
}

static void pm_heap(const PmHeader *q)
{
    u32 at=q->address?q->address:pm_system[6];
    if(!q->address && pm_system[6]>=pm_system[7]){pm_send(q->op,q->seq,0,0,0);return;}
    if(!pm_heap_range(at,16)){pm_send(q->op,q->seq,q->address,0,3);return;}
    u32 b[4];memcpy(b,(const void *)at,sizeof b);
    if(b[0]!=0x48454150u || b[1]>0xfffffff0u || !pm_heap_range(at,16+b[1]) || b[3]>1 ||
       (b[2] && (b[2]<at+16+b[1] || !pm_heap_range(b[2],16)))){
        pm_send(q->op,q->seq,q->address,0,3);return;
    }
    u32 result[]={b[2],at,b[3]?16:16+b[1],b[3]?11:10,b[1]};
    memcpy(pm_reply.data,result,sizeof result);pm_send(q->op,q->seq,q->address,sizeof result,0);
}

static void pm_receive(u8 *fr,u16 len)
{
    if(len<14)return;
    u16 type=((u16)fr[12]<<8)|fr[13];
    if(type==0x0806 && len>=14+sizeof(Arp)){
        const Arp *a=(const Arp *)(fr+14);
        if(a->htype!=htons16(1) || a->ptype!=htons16(0x0800) ||
           a->hlen!=6 || a->plen!=4 || (a->sha[0]&1))return;
        if(a->tpa==net_ip && a->op==htons16(1))arp_send(2,a->sha,a->spa);
        if(a->tpa==net_ip && a->spa==pm_hop && a->op==htons16(2)){
            memcpy(pm_mac,a->sha,6);pm_mac_ready=1;
        }
        return;
    }
    if(type!=0x0800 || len<34)return;
    const Ip *ip=(const Ip *)(fr+14);
    u32 ihl=(ip->vihl&15)*4u,total=htons16(ip->totlen);
    if((ip->vihl>>4)!=4 || ihl<20 || total<ihl+8 || total>(u32)len-14 ||
       (htons16(ip->frag)&0x3fff) || ip->proto!=17 || ip->src!=pm_ip ||
       ip->dst!=net_ip || csum(ip,(int)ihl))return;
    const u8 *udp=(const u8 *)ip+ihl;
    u16 source=((u16)udp[0]<<8)|udp[1],dest=((u16)udp[2]<<8)|udp[3];
    u16 size=((u16)udp[4]<<8)|udp[5];
    if(source!=pm_port || dest!=PM_PORT || size!=8+sizeof(PmHeader) ||
       size>total-ihl || ((udp[6]||udp[7]) && nw_l4_csum(ip->src,ip->dst,17,udp,size)))return;
    PmHeader q;memcpy(&q,udp+8,sizeof q);u32 check=q.crc;q.crc=0;
    if(q.magic!=PM_MAGIC || q.session!=pm_session || q.status || !q.seq || pm_crc(&q,sizeof q)!=check)return;
    memcpy(pm_mac,fr+6,6);pm_mac_ready=1;
    if(q.op==0 && !q.length && !q.address){pm_info(q.seq);return;}
    if(q.op==1 || q.op==6){
        int ok=q.op==1?pm_read(q.address,pm_reply.data,q.length):
            q.length && q.length<=PM_BYTES && pm_ram(q.address,q.length);
        if(ok && q.op==6)memcpy(pm_reply.data,(const void *)q.address,q.length);
        pm_send(q.op,q.seq,q.address,ok?q.length:0,ok?0:1);return;
    }
    if(q.op==2 && !q.length){pm_fault((FaultRec *)pm_reply.data,q.address);pm_send(q.op,q.seq,q.address,sizeof(FaultRec),0);return;}
    if(q.op==3){pm_blob(&q);return;}
    if(q.op==4 && !q.length){pm_region(&q);return;}
    if(q.op==5 && !q.length){pm_heap(&q);return;}
    pm_send(q.op,q.seq,q.address,0,2);
}

__attribute__((used,noreturn)) static void pm_run(void)
{
    u32 cr0;__asm__ volatile("mov %%cr0,%0":"=r"(cr0));
    if(cr0&0x80000000u){
        u32 cr4;__asm__ volatile("mov %%cr4,%0":"=r"(cr4));cr4&=~128u;
        __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
        __asm__ volatile("mov %0,%%cr3"::"r"((u32)pm_pd):"memory");
    }
    pm_active=1;pm_snapshot();
    pm_hop=(pm_ip&net_mask)==(net_ip&net_mask)?pm_ip:net_gw;
    pm_mac_ready=arp_lookup(pm_hop,pm_mac);
    u32 loops=0;
    for(;;){
        if(!(loops++&65535u)){
            if(pm_mac_ready)pm_info(0);
            else if(pm_hop){static const u8 zero[6]={0};arp_send(1,zero,pm_hop);}
        }
        nic_poll();__asm__ volatile("outb %%al,$0x80"::"a"(0));
    }
}

__attribute__((naked,noreturn)) static void pm_jump(void)
{
    __asm__ volatile("cli; cld; movl $pm_stack+8192,%esp; andl $-16,%esp; call pm_run; 1: cli; hlt; jmp 1b");
}

static void pm_frame(const u32 *frame)
{
    memcpy(&pm_cpu,frame,13*sizeof(u32));
    pm_cpu.esp=(frame[11]&3)?frame[13]:(u32)(frame+13);
    u16 ss;__asm__ volatile("mov %%ss,%0":"=r"(ss));
    pm_cpu.ss=(frame[11]&3)?frame[14]:ss;
}

static void pm_capture(const u32 *frame)
{
    if(!pm_ip)return;
    pm_frame(frame);
    __asm__ volatile("mov %%cr0,%0":"=r"(pm_cpu.cr0));
    __asm__ volatile("mov %%cr3,%0":"=r"(pm_cpu.cr3));
    __asm__ volatile("mov %%cr2,%0":"=r"(pm_cpu.cr2));
    if(pm_cpu.cr0&0x80000000u)__asm__ volatile("mov %%cr4,%0":"=r"(pm_cpu.cr4));
    pm_captured=1;pm_extra[1]=1;
}

static void pm_enter(void (*describe)(FaultRec *))
{
    (void)describe;
    if(pm_ip && pm_captured && net_ip && net_up() && pm_ram_end)pm_jump();
}

static void pm_emergency(u32 vec,u32 err,u32 eip,u32 cr2,u32 reason,u32 original,u32 ov,u32 oe,u32 oi,u32 oa)
{
    if(!pm_ip || !net_ip || !net_up() || !pm_ram_end)return;
    if(!++pm_session)pm_session=1;
    pm_extra[0]=reason+1;pm_extra[1]=0;pm_extra[6]=original;
    pm_extra[2]=ov;pm_extra[3]=oe;pm_extra[4]=oi;pm_extra[5]=oa;
    if(original==1 && pm_captured){memcpy(&pm_original,&pm_cpu,sizeof pm_cpu);pm_extra[1]|=2;}
    memset(&pm_cpu,0,sizeof pm_cpu);
    const u32 *frame=*pm_core->frame;
    if(vec==8 && pm_ram((u32)pm_core->tss,104)){
        const u32 *t=pm_core->tss;
        pm_cpu.edi=t[17];pm_cpu.esi=t[16];pm_cpu.ebp=t[15];pm_cpu.esp=t[14];
        pm_cpu.ebx=t[13];pm_cpu.edx=t[12];pm_cpu.ecx=t[11];pm_cpu.eax=t[10];
        pm_cpu.eip=t[8];pm_cpu.eflags=t[9];pm_cpu.cs=t[19];pm_cpu.ss=t[20];pm_cpu.cr3=t[7];
        pm_extra[1]|=5;
    }else if(pm_ram((u32)frame,60)){
        pm_frame(frame);pm_cpu.cr3=pm_core->controls[1];pm_extra[1]|=1;
    }
    pm_cpu.cr0=pm_core->controls[0];pm_cpu.cr4=pm_core->controls[2];
    pm_cpu.vec=vec;pm_cpu.err=err;pm_cpu.eip=eip;pm_cpu.cr2=cr2;
    pm_jump();
}
static void pm_cache(void);
static const PanicMonitor pm_ops={PANIC_MONITOR_ABI,pm_capture,pm_enter,pm_emergency};
static int pm_enable(u32 ip,u16 port)
{
    pm_core=api->service_get("panic.core");
    if(!pm_core || pm_core->abi!=PANIC_MONITOR_ABI){api->shell_print("Update the kernel to enable crash reports.\n");return 0;}
    u32 flags=net_irq_save();
    pm_ip=0;*pm_core->monitor=0;
    u32 kb=api->mem_info(MI_TOTAL_KB);
    pm_ram_end=kb>=0x100000u?0x40000000u:kb*1024u;
    u32 fb=api->boot_info(BI_LFB_ADDR);
    if(fb>=0x100000u && fb<pm_ram_end)pm_ram_end=fb;
    for(u32 i=0;i<1024;i++)pm_pd[i]=(i<<22)|0x83u;
    pm_port=port;pm_session=api->rand();if(!pm_session)pm_session=1;
    memset(pm_extra,0,sizeof pm_extra);memset(&pm_original,0,sizeof pm_original);pm_captured=0;pm_next_cache=0;
    pm_ip=ip;*pm_core->monitor=&pm_ops;
    fn_ip=ip;fn_port=(u16)(port>1?port-1:FAULTNET_PORT);fn_seen=fn_next=fn_sent=0;fn_left=0;fn_test=1;
    fn_session=pm_session;fn_state="enabled";pm_cache();
    net_irq_restore(flags);
    return 1;
}

static void pm_disable(void)
{
    pm_ip=fn_ip=0;fn_left=fn_test=0;fn_state="off";
    if(pm_core)*pm_core->monitor=0;
}

#include "debugnet.inc"

static void cmd_debugnet(const char *args)
{
    while(*args==' ')args++;
    if(!strcmp(args,"off")){
        u32 flags=net_irq_save();
        dn_stop();pm_disable();
        net_irq_restore(flags);
        api->shell_print(dn_auto?"Crash reports: off until restart.\n":"Crash reports: off\n");return;
    }
    if(!strcmp(args,"auto")){
        u32 flags=net_irq_save();dn_start();net_irq_restore(flags);
        args="";
    }
    if(!strcmp(args,"status"))args="";
    if(!strcmp(args,"test")){
        if(!pm_ip){api->shell_print("Find a manager first: debugnet auto\n");return;}
        u32 flags=net_irq_save();fn_test=1;fn_next=ticks;net_irq_restore(flags);
        api->shell_print("Test queued. Check the manager for a connection test.\n");return;
    }
    if(*args){
        char iptext[20];unsigned n=0,port=PM_PORT;u32 ip=0;
        while(*args && *args!=' ' && n<sizeof iptext-1)iptext[n++]=*args++;
        iptext[n]=0;while(*args==' ')args++;
        if(*args){
            port=0;
            while(*args>='0' && *args<='9'){
                port=port*10+(unsigned)(*args++-'0');if(port>65535)break;
            }
            while(*args==' ')args++;
        }
        if(*args || !port || port>65535 || !api->net_parse_ip(iptext,&ip) ||
           !nc_unicast(ip) || (ip&255)==127 || ip==net_ip ||
           (net_mask && (ip&net_mask)==(net_ip&net_mask) &&
            !nc_host_valid(ip,net_mask))){
            api->shell_print("Usage: debugnet auto | <PC-IP> [port] | status | test | off\n");return;
        }
        u32 flags=net_irq_save();
        int enabled=pm_enable(ip,(u16)port);if(enabled)dn_stop();
        net_irq_restore(flags);if(!enabled)return;
    }
    if(!pm_ip){
        api->shell_print(dn_search?"Looking for FLOPNIX Manager. Keep its receiver open.\n":
                        "Crash reports: off. Use debugnet auto to find a manager.\n");return;
    }
    char b[128];
    kfmt(b,sizeof b,"Crash reports -> %u.%u.%u.%u:%u\n",pm_ip&255,(pm_ip>>8)&255,(pm_ip>>16)&255,pm_ip>>24,(unsigned)pm_port);
    api->shell_print(b);
    kfmt(b,sizeof b,"%s; %u report packets sent.\n",!net_up()?"waiting for network":net_ip?fn_state:"waiting for network address",fn_sent);
    api->shell_print(b);
    api->shell_print(dn_auto?"App and system crashes enabled. Auto-connect is saved.\n":
                    "App and system crashes enabled for this boot.\n");
}

static void pm_cache(void)
{
    if(!pm_ip || (i32)(ticks-pm_next_cache)<0)return;
    pm_next_cache=ticks+100;
    const DebugOps *d=api->service_get("debug");
    if(d && d->abi==DEBUG_ABI && d->snapshot)d->snapshot(pm_debug,sizeof pm_debug);
    else pm_text(pm_debug,"Debug extension is not loaded.",sizeof pm_debug);
}
