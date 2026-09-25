/* Copies and moves files between floppy and USB storage. */
#include "filepaths.inc"
static const char *ft_leaf(const char *p)
{const char *b=p;for(;*p;p++)if(*p=='/'&&p[1])b=p+1;return b;}
static int ft_copy_name(const Kapi *k,int drive,const char *dir,const char *leaf,char *dst)
{
    const char *dot=0;for(const char *p=leaf;*p;p++)if(*p=='.')dot=p;
    const char *ext=dot?dot:"";int bl=dot?(int)(dot-leaf):(int)k->strlen(leaf);
    for(int i=1;i<100;i++){
        char suffix[8],base[64],name[80];k->kfmt(suffix,sizeof suffix,"_%d",i);
        int room=(drive?8:FS_NAMELEN-1-(int)k->strlen(dir)-(dir[0]?1:0)-(int)k->strlen(ext))-(int)k->strlen(suffix);
        if(room<1)return FT_PATH;if(bl<room)room=bl;if(room>63)room=63;
        k->memcpy(base,leaf,room);base[room]=0;k->kfmt(name,sizeof name,"%s%s%s",base,suffix,ext);
        int rc=drive?ft_usb_name(k,dir,name,dst):ft_join(k,0,dir,name,dst);
        if(rc)return rc;if(!ft_exists(k,drive,dst))return 0;
    }return FT_EXISTS;
}
static int ft_size(const Kapi *k,int drive,const char *path,u32 *size)
{
    if(!drive){
        for(int i=0;i<FS_NFILES;i++){FsEnt *e=k->fs_slot(i);if(e&&e->used&&!k->strcmp(e->name,path)){
            if(e->attr&FS_ATTR_DIR)return FT_FOLDER;*size=e->size;return 0;
        }}return FT_MISSING;
    }
    static FatEnt entries[128];char dir[64];const char *base=ft_leaf(path);
    int len=(int)(base-path);if(len>1)len--;if(len>=64)return FT_PATH;
    k->memcpy(dir,path,len);dir[len]=0;if(!len)k->strlcpy(dir,"/",sizeof dir);
    int count=k->fat_list(dir,entries,128);if(count<0)return FT_READ;
    for(int i=0;i<count;i++)if(!k->strcasecmp(entries[i].name,base)){
        if(entries[i].is_dir)return FT_FOLDER;*size=entries[i].size;return 0;
    }
    return FT_MISSING;
}
static int ft_transfer(const Kapi *,const char *,int,const char *,int,char *,int *);

static int ft_tree_copy(const Kapi *k,int sd,const char *src,int dd,const char *dst,int depth)
{
    if(depth>=8)return FT_TREE;
    FatEnt *items=k->kmalloc((sd?129:FS_NFILES)*sizeof *items);if(!items)return FT_MEMORY;
    int count=0,rc=0;
    if(sd){count=k->fat_list(src,items,129);if(count<0)rc=FT_READ;else if(count>=129)rc=FT_TREE;}
    else for(int i=0;i<FS_NFILES;i++){
        FsEnt *e=k->fs_slot(i);if(!e||!e->used)continue;
        char child[FS_NAMELEN];int kind=fs_child(e->name,src,child,sizeof child);if(!kind)continue;
        const char *leaf=ft_leaf(child);int have=0;for(int j=0;j<count;j++)if(!k->strcmp(items[j].name,leaf)){have=1;break;}
        if(have)continue;k->strlcpy(items[count].name,leaf,64);items[count++].is_dir=kind==2||(e->attr&FS_ATTR_DIR);
    }

    for(int i=0;!rc&&i<count;i++){char path[FT_PATHMAX];rc=dd?ft_usb_name(k,dst,items[i].name,path):ft_join(k,dd,dst,items[i].name,path);}
    if(!rc){int r=dd?k->fat_mkdir(dst):k->fs_mkdir(dst);if(r)rc=r==-2?FT_SPACE:FT_WRITE;}
    for(int i=0;!rc&&i<count;i++){
        char source[FT_PATHMAX],target[FT_PATHMAX];rc=ft_join(k,sd,src,items[i].name,source);if(rc)break;
        if(items[i].is_dir){
            rc=dd?ft_usb_name(k,dst,items[i].name,target):ft_join(k,dd,dst,items[i].name,target);
            if(!rc)rc=ft_tree_copy(k,sd,source,dd,target,depth+1);
        }else{
            char spec[FT_PATHMAX+2];int moved;sh_spec_make(sd,source,spec,sizeof spec);
            rc=ft_transfer(k,spec,dd,dst,FT_COPY,target,&moved);if(rc>0)rc=0;
        }
    }
    k->kfree(items);return rc;
}

static int ft_transfer(const Kapi *k,const char *spec,int drive,const char *dir,int mode,char *dst,int *moved)
{
    *moved=0;dst[0]=0;
    if(k->strlen(spec)>=FT_PATHMAX+2)return FT_PATH;
    char src[FT_PATHMAX];int sd;
    if(!sh_spec_split(spec,&sd,src,sizeof src))return FT_PATH;
    int len=k->strlen(src),folder=len&&src[len-1]=='/';if(folder)src[--len]=0;
    if(!len)return FT_PATH;
    if(!sd&&(k->fs_is_dir(src)||k->fs_dir_count(src)))folder=1;
    int move=mode==FT_MOVE||(mode==FT_DRAG&&sd==drive);
    if(!sd&&(!k->strcmp(src,"sys")||sh_is_system_file(src))&&move){
        if(mode==FT_MOVE||folder)return FT_SYSTEM;move=0;
    }
    int rc=ft_join(k,drive,dir,ft_leaf(src),dst);if(rc)return rc;
    int same=sd==drive&&!(drive?k->strcasecmp(src,dst):k->strcmp(src,dst));
    if(same&&move)return 1;
    if(same&&!move){
        if(folder&&!drive){int n;for(n=1;n<100;n++){char name[8];k->kfmt(name,sizeof name,"C%d",n);rc=ft_join(k,drive,dir,name,dst);if(rc)return rc;if(!ft_exists(k,drive,dst))break;}if(n==100)return FT_EXISTS;}
        else {rc=ft_copy_name(k,drive,dir,ft_leaf(src),dst);if(rc)return rc;}
    }
    if(folder){
        if(!sd&&!k->fs_is_dir(src)&&!k->fs_dir_count(src))return FT_MISSING;
        if(sd||drive){
            if(move)return FT_FOLDER;
            if(!k->usb_present()||(drive&&!k->fat_writable()))return FT_READONLY;
            if(drive&&!same){rc=ft_usb_name(k,dir,ft_leaf(src),dst);if(rc)return rc;}
            if(ft_exists(k,drive,dst))return FT_EXISTS;

            if(sd==drive){int n=0;while(src[n]&&dst[n]&&sh_ci_eq(src[n],dst[n]))n++;if(!src[n]&&dst[n]=='/')return FT_PATH;}
            return ft_tree_copy(k,sd,src,drive,dst,0);
        }
        if(ft_exists(k,0,dst))return FT_EXISTS;
        rc=move?k->fs_rename_dir(src,dst):folder_copy_a(k,src,dst);
        if(rc)return rc==-2?(move?FT_PATH:FT_SPACE):FT_WRITE;
        *moved=move;return 0;
    }
    if(drive){
        if(!k->usb_present()||!k->fat_writable())return FT_READONLY;
        if(!same){rc=ft_usb_name(k,dir,ft_leaf(src),dst);if(rc)return rc;}
    }
    if(ft_exists(k,drive,dst))return FT_EXISTS;

    if(move&&!sd&&!drive){if(k->fs_rename(src,dst))return FT_WRITE;*moved=1;return 0;}
    u32 size;rc=ft_size(k,sd,src,&size);if(rc)return rc;
    if(size>=0x7ffffffeu||size+1>k->heap_avail())return FT_MEMORY;
    u8 *buf=k->kmalloc(size+1);if(!buf)return FT_MEMORY;

    int got=sd?k->fat_read(src,buf,size+1):k->fs_read(src,buf,size+1);
    if(got!=(int)size){k->kfree(buf);return FT_READ;}
    rc=drive?k->fat_write(dst,buf,size):k->fs_write(dst,buf,size);k->kfree(buf);
    if(rc)return rc==-2?FT_SPACE:FT_WRITE;
    if(move){

        rc=sd?k->fat_delete(src):k->fs_delete(src);if(rc)return FT_DELETE;*moved=1;
    }return 0;
}
static void ft_batch(const Kapi *k,const char *list,int drive,const char *dir,int mode,FtBatch *b)
{
    static char work[FT_LIST];k->strlcpy(work,list,sizeof work);k->memset(b,0,sizeof *b);
    int pending=0,index=0;char *p=work;
    for(const char *s=work;*s;s++)if(*s!='\n'&&(s==work||s[-1]=='\n'))b->total++;
    k->esc_arm();
    while(*p){char *end=p;while(*end&&*end!='\n')end++;char save=*end;*end=0;
        if(*p){int moved=0;char dst[FT_PATHMAX],label[FT_PATHMAX+24];
            k->kfmt(label,sizeof label,"%d of %d: %s",++index,b->total,ft_leaf(p));
            if(!b->cancelled)k->busy_set(mode==FT_COPY?"Copying (Esc to stop)":"Transfer (Esc to stop)",label,(index-1)*256/b->total);
            if(k->esc_pending())b->cancelled=1;
            int rc=b->cancelled?0:ft_transfer(k,p,drive,dir,mode,dst,&moved);
            if(b->cancelled||rc<0){
                if(b->cancelled||rc==FT_EXISTS)b->skipped++;else b->failed++;
                if(rc==FT_DELETE)b->copied++;
                if(rc<0&&!b->first[0]){k->strlcpy(b->first,ft_reason(rc),sizeof b->first);k->strlcpy(b->first_file,p,sizeof b->first_file);}
                int n=k->strlen(p);if(pending+n+2<FT_LIST){if(pending)b->pending[pending++]='\n';k->memcpy(b->pending+pending,p,n+1);pending+=n;}
            }else if(rc==1){b->same++;b->skipped++;}
            else{if(moved)b->moved++;else b->copied++;k->strlcpy(b->last,ft_leaf(dst),sizeof b->last);}
        }if(!save)break;p=end+1;
    }k->busy_end();k->broadcast("file.changed","");k->gui_dirty();
}
static void ft_message(const Kapi *k,const FtBatch *b,char *out,int cap)
{
    if(b->total==1&&!b->skipped&&!b->failed)k->kfmt(out,cap,"1 %s: %s",b->moved?"moved":"copied",b->last);
    else k->kfmt(out,cap,"%d copied, %d moved, %d skipped, %d failed",b->copied,b->moved,b->skipped,b->failed);
}
static void ft_report(const Kapi *k,const FtBatch *b)
{
    if(!b->failed&&!b->cancelled&&b->skipped==b->same)return;
    char summary[64],text[288];ft_message(k,b,summary,sizeof summary);
    k->kfmt(text,sizeof text,"%s. %s\n%s\n%s",summary,
        b->cancelled?"Stopped; completed items kept.":"Completed items kept.",b->first_file,b->first);
    k->msgbox(b->cancelled?"Transfer stopped":"Transfer results",text,MB_OK,0,0);
}
static int ft_clip_get(const Kapi *k,char *list,int *mode)
{
    *mode=FT_MOVE;int n=k->clip_get("file.cut",list,FT_LIST);
    if(n<=0){*mode=FT_COPY;n=k->clip_get("file",list,FT_LIST);}list[FT_LIST-1]=0;return n>0&&list[0];
}
static void ft_clip_finish(const Kapi *k,const char *original,const FtBatch *b)
{
    static char now[FT_LIST];int n=k->clip_get("file.cut",now,sizeof now);now[FT_LIST-1]=0;

    if(n>0&&!k->strcmp(now,original))k->clip_set("file.cut",b->pending,k->strlen(b->pending)+1);
}
