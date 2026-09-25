enum {
    DH_UNAVAILABLE=-1, DH_INCOMPLETE=-2,
    DH_BOUNDS=1, DH_MAGIC, DH_SIZE, DH_FREE, DH_LINK
};

static int debug_heap_check(u32 *bad)
{
    _Static_assert(sizeof(DebugHeapBlock)==16,"debug heap block layout");
    u32 f=lock();
    u32 base=api->mem_info(MI_HEAP_BASE),end=api->mem_info(MI_HEAP_END);
    u32 grow=api->mem_info(MI_HEAP_GROW_BASE),top=api->mem_info(MI_HEAP_GROW_END);
    u32 at=base;
    int result=DH_BOUNDS;
    if((base|end|grow|top)&7u)goto done;
    if(end<base||grow<end||top<grow)goto done;
    if(end==base&&top==grow){result=DH_UNAVAILABLE;goto done;}
    if(!base||end-base<sizeof(DebugHeapBlock)+8)goto done;
    for(u32 count=0;count<4096;count++){
        u32 limit=end;
        if(grow==end||at>=grow)limit=top;
        if((at&7u)||at<base||at>=limit||
           (at>=end&&at<grow)||limit-at<sizeof(DebugHeapBlock)){
            result=DH_BOUNDS;goto done;
        }
        const DebugHeapBlock *b=(const DebugHeapBlock *)at;
        if(b->magic!=DEBUG_HEAP_MAGIC){result=DH_MAGIC;goto done;}
        u32 size=b->size;
        if(size<8||(size&7u)||size>limit-at-sizeof *b){result=DH_SIZE;goto done;}
        if(b->free>1){result=DH_FREE;goto done;}
        u32 next=at+sizeof *b+size;
        if(next==end&&grow>end)next=top>grow?grow:0;
        else if(next==limit)next=0;
        if((u32)b->next!=next){result=DH_LINK;goto done;}
        if(!next){result=0;goto done;}
        at=next;
    }
    result=DH_INCOMPLETE;
done:
    if(bad)*bad=result>0||result==DH_INCOMPLETE?at:0;
    unlock(f);
    return result;
}
