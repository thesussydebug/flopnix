/* Copies and moves files between floppy and USB storage. */
enum { FT_COPY, FT_DRAG, FT_MOVE };
enum { FT_PATH=-1, FT_MISSING=-2, FT_EXISTS=-3, FT_MEMORY=-4,
       FT_READ=-5, FT_WRITE=-6, FT_SPACE=-7, FT_READONLY=-8,
       FT_FOLDER=-9, FT_SYSTEM=-10, FT_DELETE=-11, FT_TREE=-12 };
#define FT_LIST 4096
#define FT_PATHMAX 96
typedef struct {
    int total, copied, moved, same, failed;
    char first[64], last[FT_PATHMAX], pending[FT_LIST];
} FtBatch;

static const char *ft_leaf(const char *p)
{const char *b=p;for(;*p;p++)if(*p=='/'&&p[1])b=p+1;return b;}
static const char *ft_reason(int rc)
{
    switch(rc){
    case FT_PATH:return "Path too long or invalid; shorten the name";
    case FT_MISSING:return "Source is missing or cannot be listed";
    case FT_EXISTS:return "Name already exists; nothing overwritten";
    case FT_MEMORY:return "Not enough free RAM to copy this file";
    case FT_READ:return "Could not read the complete source";
    case FT_SPACE:return "Destination is full";
    case FT_READONLY:return "USB is missing or read-only";
    case FT_FOLDER:return "Use Copy for USB folders; originals are kept";
    case FT_TREE:return "Folder is too deep or has too many entries";
    case FT_SYSTEM:return "System files must stay in sys";
    case FT_DELETE:return "Copied, but source could not be removed";
    default:return "Destination write failed; source kept";
    }
}

static int ft_join(const Kapi *k,int drive,const char *dir,const char *leaf,char *out)
{
    int d=k->strlen(dir),n=k->strlen(leaf),slash=d&&dir[d-1]!='/';
    if(!n||k->strcmp(leaf,".")==0||k->strcmp(leaf,"..")==0)return FT_PATH;
    for(int i=0;i<n;i++)if((u8)leaf[i]<32||leaf[i]=='/'||leaf[i]=='\\'||leaf[i]==':')return FT_PATH;

    if((drive&&d>=64)||d+slash+n>=(drive?FT_PATHMAX:FS_NAMELEN))return FT_PATH;
    k->kfmt(out,FT_PATHMAX,"%s%s%s",dir,slash?"/":"",leaf);return 0;
}
static int ft_exists(const Kapi *k,int drive,const char *path)
{return drive?k->fat_exists(path):(k->fs_exists(path)||k->fs_dir_count(path));}

static int ft_usb_name(const Kapi *k,const char *dir,const char *leaf,char *dst)
{
    const char *dot=0;for(const char *p=leaf;*p;p++)if(*p=='.')dot=p;
    int bl=dot?(int)(dot-leaf):(int)k->strlen(leaf),el=dot?k->strlen(dot+1):0;
    int plain=bl>0&&bl<=8&&el<=3;
    char stem[9],ext[4];int sn=0,en=0;
    for(int i=0;i<bl;i++){
        char c=leaf[i];if(c>='a'&&c<='z')c-=32;
        int ok=(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='~';
        if(!ok)plain=0;if(sn<8)stem[sn++]=ok?c:'_';
    }
    if(!sn)stem[sn++]='_';stem[sn]=0;
    for(int i=0;i<el;i++){
        char c=dot[1+i];if(c>='a'&&c<='z')c-=32;
        int ok=(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
        if(!ok)plain=0;if(en<3)ext[en++]=ok?c:'_';
    }
    ext[en]=0;if(plain)return ft_join(k,1,dir,leaf,dst);
    for(int i=1;i<1000;i++){
        char alias[16],suffix[6],base[9];k->kfmt(suffix,sizeof suffix,"~%d",i);
        int room=8-k->strlen(suffix);k->strlcpy(base,stem,room+1);
        k->kfmt(alias,sizeof alias,"%s%s%s%s",base,suffix,en?".":"",ext);
        int rc=ft_join(k,1,dir,alias,dst);if(rc)return rc;
        if(!ft_exists(k,1,dst))return 0;
    }
    return FT_EXISTS;
}
static int ft_copy_name(const Kapi *k,int drive,const char *dir,const char *leaf,char *dst)
{
    const char *dot=0;for(const char *p=leaf;*p;p++)if(*p=='.')dot=p;
    const char *ext=dot?dot:"";int bl=dot?(int)(dot-leaf):(int)k->strlen(leaf);
    for(int i=1;i<100;i++){
        char suffix[8],base[64],name[80];k->kfmt(suffix,sizeof suffix,"_%d",i);
        int room=(drive?8:23-(int)k->strlen(dir)-(dir[0]?1:0)-(int)k->strlen(ext))-(int)k->strlen(suffix);
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
    FatEnt *items=k->kmalloc(129*sizeof *items);if(!items)return FT_MEMORY;
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
    int pending=0;char *p=work;
    while(*p){char *end=p;while(*end&&*end!='\n')end++;char save=*end;*end=0;
        if(*p){int moved=0;char dst[FT_PATHMAX];b->total++;
            k->busy_set(mode==FT_COPY?"Copying":"Transferring",ft_leaf(p),-1);
            int rc=ft_transfer(k,p,drive,dir,mode,dst,&moved);
            if(rc<0){
                b->failed++;if(!b->first[0])k->strlcpy(b->first,ft_reason(rc),sizeof b->first);
                int n=k->strlen(p);if(pending+n+2<FT_LIST){if(pending)b->pending[pending++]='\n';k->memcpy(b->pending+pending,p,n+1);pending+=n;}
            }else if(rc==1)b->same++;
            else{if(moved)b->moved++;else b->copied++;k->strlcpy(b->last,ft_leaf(dst),sizeof b->last);}
        }if(!save)break;p=end+1;
    }k->busy_end();k->broadcast("file.changed","");k->gui_dirty();
}
static void ft_message(const Kapi *k,const FtBatch *b,char *out,int cap)
{
    if(b->failed){if(b->total==1)k->strlcpy(out,b->first,cap);else k->kfmt(out,cap,"%d of %d done: %s",b->total-b->failed,b->total,b->first);}
    else if(b->total==b->same)k->strlcpy(out,"Already in this folder",cap);
    else if(b->copied+b->moved==1)k->kfmt(out,cap,"%s: %s",b->moved?"Moved":"Copied",b->last);
    else if(b->copied&&b->moved)k->kfmt(out,cap,"%d copied, %d moved",b->copied,b->moved);
    else k->kfmt(out,cap,"%d items %s",b->copied+b->moved,b->moved?"moved":"copied");
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
