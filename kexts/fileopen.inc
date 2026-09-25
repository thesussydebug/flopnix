#pragma once

#define FO_LIMIT 0x7FFFFFFEu
enum { FO_READ=-1, FO_MEMORY=-2, FO_SIZE=-3, FO_FOLDER=-4, FO_CANCELLED=-5 };
typedef struct { u8 *data; u32 size, capacity; } FileData;

static inline const char *fo_error(int rc)
{
    if(rc==FO_MEMORY)return "Not enough memory to open file";
    if(rc==FO_SIZE)return "File is too large for this system";
    if(rc==FO_FOLDER)return "Cannot open a folder as a file";
    if(rc==FO_CANCELLED)return "File open cancelled";
    return "Could not read the complete file";
}

static inline void fo_release(const Kapi *k,FileData *f)
{
    if(f->data&&f->data!=k->iobuf)k->kfree(f->data);
    f->data=0;f->size=f->capacity=0;
}

static inline int fo_read(const Kapi *k,int drive,const char *path,u32 size,
                          int private_buffer,FileData *f)
{
    f->data=0;f->size=f->capacity=0;
    if(size>FO_LIMIT)return FO_SIZE;
    u32 cap=size+1;
    u8 *data=!private_buffer&&cap<=k->iobuf_size?k->iobuf:k->kmalloc(cap);
    if(!data)return FO_MEMORY;
    if(data!=k->iobuf&&k->mem_track)k->mem_track("File open buffer",data,cap);
    int n=drive?k->fat_read(path,data,cap):k->fs_read(path,data,cap);
    if(n<0||(u32)n!=size){if(data!=k->iobuf)k->kfree(data);return FO_READ;}
    data[size]=0;f->data=data;f->size=size;f->capacity=cap;return 0;
}

static inline int fo_load(const Kapi *k,int drive,const char *path,
                          int private_buffer,FileData *f,int (*cancelled)(void))
{
    f->data=0;f->size=f->capacity=0;
    if(!drive){
        for(int i=0;i<FS_NFILES;i++){
            const FsEnt *e=k->fs_slot(i);
            if(e&&e->used&&!k->strcmp(e->name,path)){
                if(e->attr&FS_ATTR_DIR)return FO_FOLDER;
                int r=fo_read(k,0,path,e->size,private_buffer,f);
                if(cancelled&&cancelled()){fo_release(k,f);return FO_CANCELLED;}
                return r;
            }
        }
        return FO_READ;
    }
    u32 cap=!private_buffer&&k->iobuf_size>1?k->iobuf_size:4097;
    if(cap>FO_LIMIT+1)cap=FO_LIMIT+1;
    u8 *data=private_buffer||cap!=k->iobuf_size?k->kmalloc(cap):k->iobuf;
    if(!data)return FO_MEMORY;
    for(;;){
        if(data!=k->iobuf&&k->mem_track)k->mem_track("File open buffer",data,cap);
        int n=k->fat_read(path,data,cap),r=0;
        if(cancelled&&cancelled())r=FO_CANCELLED;
        else if(n<0||(u32)n>cap)r=FO_READ;
        else if((u32)n<cap){
            data[n]=0;f->data=data;f->size=(u32)n;f->capacity=cap;return 0;
        }else if(cap==FO_LIMIT+1)r=FO_SIZE;
        if(r){if(data!=k->iobuf)k->kfree(data);return r;}
        u32 next_cap=cap>(FO_LIMIT+1)/2?FO_LIMIT+1:cap*2;
        u8 *next=data==k->iobuf?k->kmalloc(next_cap):k->krealloc(data,next_cap);
        if(!next){if(data!=k->iobuf)k->kfree(data);return FO_MEMORY;}
        data=next;cap=next_cap;
    }
}
