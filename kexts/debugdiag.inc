static u32 diag_tick,device_count,device_usb,device_generation,device_link,device_adapter,device_ip,device_dhcp;
static int device_valid,heap_status=-3;
static u32 heap_bad,queue_depth,queue_peak,queue_dropped,slow_count;
static char device_line[4][112],slow_line[2][88],slow_active[88];

static void device_add(const char *text)
{
    u32 f=lock();
    api->kfmt(device_line[device_count%4],sizeof device_line[0],"[%us] %s",*api->ticks/100,text);
    device_count++;unlock(f);
}
static void device_poll(void)
{
    u32 usb=api->usb_present(),gen=api->usb_gen(),link=api->net_get(NET_LINK);
    u32 adapter=api->net_get(NET_ADAPTER),ip=api->net_get(NET_IP),dhcp=api->net_get(NET_DHCP_OK);
    u32 before=device_count;char s[88];
    if(!device_valid||usb!=device_usb||gen!=device_generation)
        device_add(!usb?"USB disconnected":device_valid&&device_usb?"USB replaced":"USB connected");
    if(!device_valid||link!=device_link||adapter!=device_adapter){
        if(!adapter)api->strlcpy(s,"Network unavailable",sizeof s);
        else if(!(link&NET_LINK_PHY))api->strlcpy(s,"Network link status unavailable",sizeof s);
        else if(!(link&NET_LINK_UP))api->strlcpy(s,"Network link down",sizeof s);
        else if(!NET_LINK_MBPS(link))api->strlcpy(s,"Network link up (speed unavailable)",sizeof s);
        else api->kfmt(s,sizeof s,"Network link up (%u Mbps)",NET_LINK_MBPS(link));
        device_add(s);
    }
    if(!device_valid||ip!=device_ip||dhcp!=device_dhcp){
        api->kfmt(s,sizeof s,"IP %u.%u.%u.%u; DHCP %s",ip&255,(ip>>8)&255,(ip>>16)&255,ip>>24,dhcp?"ready":"not ready");
        device_add(s);
    }
    device_usb=usb;device_generation=gen;device_link=link;device_adapter=adapter;
    device_ip=ip;device_dhcp=dhcp;device_valid=1;
    if(before!=device_count)api->gui_dirty();
}
static void heap_message(char *out,u32 cap)
{
    static const char *reason[]={"OK","bounds","magic","size","free flag","link"};
    if(heap_status==0)api->strlcpy(out,"Heap integrity: OK",cap);
    else if(heap_status<0)api->strlcpy(out,heap_status==-1?"Heap integrity: unavailable":
        heap_status==-2?"Heap integrity: incomplete (4096 blocks)":"Heap integrity: waiting",cap);
    else api->kfmt(out,cap,"Heap FAILED: %s @%08x",heap_status<6?reason[heap_status]:"invalid",heap_bad);
}
static void diagnostics_poll(void)
{
    if(ops.flags&DBG_BACKLOG){
        u32 f=lock(),depth=core->queue_depth(),peak=core->queue_peak(),lost=core->queue_dropped();unlock(f);
        if(depth!=queue_depth||peak!=queue_peak||lost!=queue_dropped){
            queue_depth=depth;queue_peak=peak;queue_dropped=lost;api->gui_dirty();
        }
    }
    if(ops.flags&DBG_SLOW){
        u32 age=0;int oldest=-1;char line[88];
        u32 f=lock();
        for(int i=0;i<api->win_max();i++){
            u32 elapsed,progress,io;
            if(core->job_info(i,&elapsed,&progress,&io)&&elapsed>=10&&elapsed>age){oldest=i;age=elapsed;}
        }
        const Win *w=oldest<0?0:api->win_slot(oldest);
        if(w)api->kfmt(line,sizeof line,"Running: %s %ums",w->tbuf_on?w->tbuf:w->title,(age/10)*100);
        else line[0]=0;
        unlock(f);
        f=lock();int changed=api->strcmp(line,slow_active)!=0;
        if(changed)api->strlcpy(slow_active,line,sizeof slow_active);
        unlock(f);if(changed)api->gui_dirty();
    }
    if((u32)(*api->ticks-diag_tick)<100)return;
    diag_tick=*api->ticks;
    if(ops.flags&DBG_DEVICE)device_poll();
    if((ops.flags&DBG_HEAP)&&heap_status<=0){
        u32 bad=0;int status=debug_heap_check(&bad);
        if(status!=heap_status||bad!=heap_bad){heap_status=status;heap_bad=bad;api->gui_dirty();}
    }
}
static void diagnostics_snapshot(char *out,u32 cap)
{
    char heap[88];heap_message(heap,sizeof heap);
    api->kfmt(out,cap,"%s\nInput backlog %u; peak %u; dropped %u (since boot)\nSlow handlers >=100ms: %u completed\n%s\n%s\n%s\nDevice events %u (1s samples)\n%s\n%s\n%s\n%s\n",
        heap,queue_depth,queue_peak,queue_dropped,slow_count,slow_active,slow_line[0],slow_line[1],
        device_count,device_line[0],device_line[1],device_line[2],device_line[3]);
}
