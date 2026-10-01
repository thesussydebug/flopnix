static const NetUpdateOps *push_transport;
static int push_listening;
static u32 push_progress;
static struct { u32 crc, sect, i; u8 *buf; } boot_sum = {0, 1, 0, 0};
/* reads one more track of the installed kernel: 1 = more left, 0 = done, -1 = failed. */
static int boot_sum_step(void)
{
    const FloppyOps *disk=api->service_get("disk.floppy");
    int batch=disk&&disk->abi==FLOPPY_ABI;u32 count=batch?FLOPPY_TRACK_SECTORS:1;
    if(!boot_sum.buf){
        if(!(boot_sum.buf=api->kmalloc(FLOPPY_TRACK_SECTORS*512)))return -1;
        api->mem_track("Kernel comparison",boot_sum.buf,FLOPPY_TRACK_SECTORS*512);
    }
    u32 i=boot_sum.i,n=boot_sum.sect-i,track=FLOPPY_TRACK_SECTORS-(i+1)%FLOPPY_TRACK_SECTORS;
    if(n>count)n=count;if(n>track)n=track;
    if(batch?disk->read(i+1,boot_sum.buf,n):api->disk_read(i+1,boot_sum.buf))return -1;
    if(!i){boot_sum.sect=(u32)(boot_sum.buf[6]|boot_sum.buf[7]<<8);if(boot_sum.buf[0]!=0xeb||boot_sum.sect<8||boot_sum.sect>255)return -1;}
    boot_sum.crc=ku_crc_feed(boot_sum.crc,boot_sum.buf,n*512);boot_sum.i=i+n;
    return boot_sum.i<boot_sum.sect;
}
static int push_data(const u8 *data,int n,void *ctx)
{
    Download *d=ctx;if(!receive(data,n,ctx))return 0;
    u32 progress=d->len*256/d->limit;
    if(progress!=push_progress){
        push_progress=progress;
        char msg[64];api->kfmt(msg,sizeof msg,"Downloading: %u / %u KiB",d->len/1024,d->limit/1024);
        api->busy_set("Kernel update",msg,(int)progress);
        push_transport->status(KU_DOWNLOADING,progress,msg);
    }
    return 1;
}
static void push_tick(void *ctx)
{
    (void)ctx;if(busy)return;
    push_transport=api->service_get("net.update");
    const NetHttpOps *http=api->service_get("net.http");
    if(!push_transport||push_transport->abi!=NET_UPDATE_ABI||!http||http->abi!=NET_HTTP_ABI)return;
    if(!push_listening){
        int r=boot_sum_step();if(r>0)return;
        api->kfree(boot_sum.buf);boot_sum.buf=0;
        push_transport->listen(r?0:boot_sum.crc);push_listening=1;
    }
    KuOffer offer;if(!push_transport->take(&offer))return;
    busy=1;closing=0;ready=0;release=offer.release;push_progress=~0u;
    Download d={0};d.limit=release.size;
    const char *error=0;u32 outcome=KU_FAILED;char err[100];int shared=0;
    api->esc_arm();
    api->busy_set("Kernel update","Checking the installed kernel...",0);
    int match=current_matches();
    if(match<0){error="Cannot read the boot floppy. Kernel unchanged.";goto done;}
    if(match){error="This kernel is already installed.";outcome=KU_CURRENT;goto done;}
    if(!db_reserve(api,&d.buf,release.size,1,release.size,KU_MAX,"Kernel download")){
        if(api->iobuf_size<release.size){error="Not enough memory for the update. Close apps and resend.";goto done;}
        api->buffer_lock();shared=1;d.buf.data=api->iobuf;d.buf.capacity=release.size;
    }
    char host[40],path[40];
    api->kfmt(host,sizeof host,"%u.%u.%u.%u:%u",offer.ip&255,(offer.ip>>8)&255,(offer.ip>>16)&255,offer.ip>>24,offer.port);
    api->kfmt(path,sizeof path,"/push/%08x/%08x.ku",offer.session,offer.job);
    d.started=*api->ticks;NetHttpInfo info;api->memset(&info,0,sizeof info);
    push_transport->status(KU_DOWNLOADING,0,"Downloading the kernel");
    api->busy_set("Kernel update","Downloading - Esc cancels",0);
    int result=http->get(offer.ip,(u16)offer.port,host,path,push_data,&d,1000,&info);
    if(result!=200||d.failed||(info.length_known&&info.length!=d.len)){
        error=d.failed==3?"Download timed out. Kernel unchanged.":
            d.failed==4?"Invalid download size. Kernel unchanged.":
            "Download failed. Check Manager and the network, then resend.";goto done;
    }
    api->busy_set("Kernel update","Verifying the download...",256);
    push_transport->status(KU_VERIFYING,256,"Verifying the download");
    if(!ku_image(d.buf.data,d.len,&release)){error="Kernel verification failed. Nothing was installed.";goto done;}
    if(api->esc_pending()){error="Update cancelled. Kernel unchanged.";goto done;}
    push_transport->status(KU_INSTALLING,0,"Installing - keep power on and the floppy inserted");
    api->buffer_lock();result=api->kernel_update_data(d.buf.data,d.len,err,sizeof err);api->buffer_unlock();
    if(result)error=err;
    else {error="Kernel installed. Restart FLOPNIX.";outcome=KU_INSTALLED;}
done:
    if(outcome==KU_FAILED&&(d.failed==1||api->esc_pending())&&error!=err){
        error="Update cancelled. Kernel unchanged.";outcome=KU_CANCELLED;
    }
    if(shared)api->buffer_unlock();else db_free(api,&d.buf);api->busy_end();
    push_transport->status(outcome,outcome==KU_CURRENT||outcome==KU_INSTALLED?256:0,error);
    say(error);api->notify(error);busy=0;
}
