#include "os.h"
#include "debug.h"

static const DebugOps *sink;
static const char *paths[THR_MAX][2];

static void debug_bind(const DebugOps *ops)
{
    if(ops&&ops->abi==DEBUG_ABI)sink=ops;
}

static void debug_place(int type)
{
    for(int i=0;i<MAXWIN;i++)if(wins[i].used&&wins[i].type==type){
        wins[i].x=4;wins[i].y=SH-TBH-wins[i].h-4;
        if(wins[i].y<0)wins[i].y=0;gui_dirty=1;
    }
}

const DebugCore debug_core={DEBUG_ABI,debug_bind,thread_guard_mask,fat_append,debug_place};

void debug_event(u32 kind,const char *name,u32 a,u32 b,int result)
{
    if(sink&&(sink->flags&kind))sink->event(kind,name,a,b,result);
}

const char *debug_path(int usb,const char *path)
{
    const char *old=paths[thr_self][usb];paths[thr_self][usb]=path;return old;
}

void debug_disk(int usb,int write,u32 lba,u32 count,int result)
{
    debug_event(DBG_DISK|(result?DBG_FS:0),paths[thr_self][usb],lba,
                count|(usb?0x80000000u:0)|(write?0x40000000u:0),result);
}

void debug_draw(void)
{
    if(sink&&sink->flags){clear_clip();sink->draw();}
}

int debug_done(int mode,const char *old,int result)
{
    int usb=mode&1;
    if(result<0)debug_event(DBG_FS,paths[thr_self][usb],~0u,
        (usb?0x80000000u:0)|(mode&2?0x40000000u:0),result);
    paths[thr_self][usb]=old;return result;
}

void debug_unwind(void){paths[thr_self][0]=paths[thr_self][1]=0;}
