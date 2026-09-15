static struct { u8 *data; char name[FS_NAMELEN]; u32 size, stamp; u16 start; } fc[8];
static u32 fc_clock;
static u32 fc_epoch;

void fs_cache_clear(void)
{
    preempt_disable();
    for(int i=0;i<8;i++)if(fc[i].data){u8 *p=fc[i].data;fc[i].data=0;kfree(p);}
    preempt_enable();
}
void fs_cache_invalidate(u32 lba)
{
    if(lba<FS_SUPER||lba>=FS_END)return;
    preempt_disable();fc_epoch++;
    if(lba<FS_DATA){if(!fs_mutex.held||fs_mutex.owner!=thr_self)fs_cache_clear();}
    else for(int i=0;i<8;i++)if(fc[i].data&&lba>=fc[i].start&&lba-fc[i].start<(fc[i].size+511)/512){
        u8 *p=fc[i].data;fc[i].data=0;kfree(p);
    }
    preempt_enable();
}
static int fc_read(const FsEnt *e,u8 *buf,u32 n)
{
    preempt_disable();
    for(int i=0;i<8;i++)if(fc[i].data&&fc[i].size==e->size&&fc[i].start==e->start&&!strcmp(fc[i].name,e->name)){
        memcpy(buf,fc[i].data,n);fc[i].stamp=++fc_clock;preempt_enable();return 1;
    }
    preempt_enable();return 0;
}
static void fc_save(const FsEnt *e,const u8 *buf,u32 n,u32 epoch)
{
    u32 budget=(heap_end()-heap_base())/4;
    if(budget>524288)budget=524288;
    if(!n||n!=e->size||n>budget)return;
    preempt_disable();
    if(epoch!=fc_epoch){preempt_enable();return;}
    int chosen=-1;
    for(int pass=0;pass<9;pass++){
        u32 used=0;int oldest=-1;chosen=-1;
        for(int i=0;i<8;i++){
            if(fc[i].data){used+=fc[i].size;if(oldest<0||fc[i].stamp<fc[oldest].stamp)oldest=i;}
            else chosen=i;
        }
        if(chosen>=0&&used+n<=budget)break;
        if(oldest<0)break;
        kfree(fc[oldest].data);fc[oldest].data=0;
    }
    if(chosen>=0&&heap_avail()>n+65536){
        u8 *p=kmalloc(n);
        if(p){memcpy(p,buf,n);fc[chosen].data=p;fc[chosen].size=n;fc[chosen].start=e->start;
            strlcpy(fc[chosen].name,e->name,FS_NAMELEN);fc[chosen].stamp=++fc_clock;mem_track("Floppy cache",p,n);}
    }
    preempt_enable();
}
