/* Checks a folder tree before copying its files. */
#include "filepaths.inc"
static int folder_copy_b(const Kapi *k,const char *src,const char *dst,char (*names)[FS_NAMELEN],int cap)
{
    char (*targets)[FS_NAMELEN]=names+cap;
    u32 *sizes=(u32 *)(targets+cap);u8 *dirs=(u8 *)(sizes+cap);
    int n=0,free=0;u32 sectors=0;
    for(int i=0;i<FS_NFILES;i++){
        FsEnt *e=k->fs_slot(i);if(!e||!e->used){free++;continue;}
        if(!fs_under(e->name,src))continue;
        if(n>=cap)return -1;
        if(!fs_rejoin(e->name,src,dst,targets[n],ft_namecap(k))||k->fs_exists(targets[n]))return -1;
        k->strlcpy(names[n],e->name,FS_NAMELEN);sizes[n]=e->size;dirs[n]=!!(e->attr&FS_ATTR_DIR);
        if(dirs[n]&&!fs_dirname_ok(targets[n]))return -1;
        if(!dirs[n])sectors+=(e->size+511)/512+(e->size==0);
        n++;
    }
    if(!n&&!k->fs_is_dir(src))return -1;
    if(free<n+1||sectors>k->fs_free_kb()*2)return -2;
    if(k->fs_mkdir(dst))return -3;
    for(int i=0;i<n;i++){
        if(dirs[i]){if(k->fs_mkdir(targets[i]))return -3;continue;}
        if(sizes[i]>k->heap_avail())return -2;
        u8 *buf=k->kmalloc(sizes[i]?sizes[i]:1);if(!buf)return -2;
        int got=k->fs_read(names[i],buf,sizes[i]);
        int failed=got!=(int)sizes[i]||k->fs_write(targets[i],buf,sizes[i]);k->kfree(buf);
        if(failed)return -3;
    }
    return 0;
}
static int folder_copy_a(const Kapi *k,const char *src,const char *dst)
{
    if(!fs_dirname_ok(dst)||!k->strcmp(src,dst)||fs_under(dst,src)||k->fs_exists(dst)||k->fs_dir_count(dst))return -1;
    int cap=1;
    for(int i=0;i<FS_NFILES;i++){FsEnt *e=k->fs_slot(i);if(e&&e->used&&fs_under(e->name,src))cap++;}
    char (*names)[FS_NAMELEN]=k->kmalloc((u32)cap*(2*FS_NAMELEN+5));
    if(!names)return -2;
    int r=folder_copy_b(k,src,dst,names,cap);
    k->kfree(names);
    return r;
}
