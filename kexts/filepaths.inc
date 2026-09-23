#pragma once
enum { FT_COPY, FT_DRAG, FT_MOVE };
enum { FT_PATH=-1, FT_MISSING=-2, FT_EXISTS=-3, FT_MEMORY=-4,
       FT_READ=-5, FT_WRITE=-6, FT_SPACE=-7, FT_READONLY=-8,
       FT_FOLDER=-9, FT_SYSTEM=-10, FT_DELETE=-11, FT_TREE=-12 };
#define FT_LIST 4096
#define FT_PATHMAX 96
typedef struct {
    int total, copied, moved, same, failed, skipped, cancelled;
    char first[64], first_file[FT_PATHMAX+2], last[FT_PATHMAX], pending[FT_LIST];
} FtBatch;

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
