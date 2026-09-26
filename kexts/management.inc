#include "manager.h"
#include "debug.h"
#include "inputpost.h"
#include "screentile.inc"

static u32 managed_expected_size,managed_expected_crc,managed_usb_gen;

static void mj(const char *s){while(*s)emit(*s++,0);}
static void mj_string(const char *s)
{
    static const char hex[]="0123456789abcdef";
    emit('"',0);
    if(s)for(;*s;s++){
        u8 c=(u8)*s;
        if(c=='"'||c=='\\'){emit('\\',0);emit((char)c,0);}
        else if(c<32){mj("\\u00");emit(hex[c>>4],0);emit(hex[c&15],0);}
        else emit((char)c,0);
    }
    emit('"',0);
}
static void mj_number(u32 n){char b[16];api->kfmt(b,sizeof b,"%u",n);mj(b);}
static void mj_signed(int n){char b[16];api->kfmt(b,sizeof b,"%d",n);mj(b);}
static void mj_key(const char *s){mj_string(s);mj(":");}
static void mj_value(const char *s,u32 n){mj(",");mj_key(s);mj_number(n);}
static void mj_text(const char *s,const char *v){mj(",");mj_key(s);mj_string(v);}
static void mj_ip(const char *key,int selector)
{
    u32 ip=api->net_get(selector);char b[24];
    api->kfmt(b,sizeof b,"%u.%u.%u.%u",ip&255,(ip>>8)&255,(ip>>16)&255,ip>>24);
    mj_text(key,b);
}
static int managed_path(const char **args,char *path)
{
    const char *s=*args;int n=0;
    while(*s&&*s!=' '){
        int a=digit(*s++);if(a<0||!*s||*s==' ')return 0;
        int b=digit(*s++),c=(a<<4)|b;
        if(b<0||c<32||c>126||n>=254)return 0;
        path[n++]=(char)c;
    }
    path[n]=0;while(*s==' ')s++;*args=s;
    if(n<3||path[1]!=':'||path[2]!='/'||(path[0]!='a'&&path[0]!='u'))return 0;
    if((path[0]=='a'&&n-3>=FS_NAMELEN)||(path[0]=='u'&&n>=192))return 0;
    for(int start=3,i=3;i<=n;i++){
        int c=(u8)path[i];
        if(c=='/'||!c){
            int len=i-start;
            if((!len&&n!=3)||(len==1&&path[start]=='.')||
               (len==2&&path[start]=='.'&&path[start+1]=='.'))return 0;
            start=i+1;
        }else if(c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return 0;
    }
    return 1;
}
static int managed_protected(const char *p)
{
    return p[0]=='a'&&(!api->strcmp(p+3,"sys")||!api->strncmp(p+3,"sys/",4));
}
static int managed_stat(const char *p,u32 *size,u32 *stamp,int *dir)
{
    *size=*stamp=0;*dir=0;
    if(p[0]=='a'){
        if(!api->fs_slot(0))return -1;
        if(!p[3]){*dir=1;return 1;}
        u32 len=api->strlen(p+3);int implicit=0;
        for(int i=0;i<FS_NFILES;i++){
            FsEnt *e=api->fs_slot(i);if(!e||!e->used)continue;
            if(!api->strcmp(e->name,p+3)){*size=e->size;*stamp=e->mtime;*dir=!!(e->attr&FS_ATTR_DIR);return 1;}
            if(!api->strncmp(e->name,p+3,len)&&e->name[len]=='/')implicit=1;
        }
        *dir=implicit;return implicit;
    }
    if(!api->fat_mount())return -1;
    if(!p[3]){*dir=1;return 1;}
    char parent[256];api->strlcpy(parent,p+2,sizeof parent);int at=0;
    for(int i=0;parent[i];i++)if(parent[i]=='/')at=i;
    const char *base=p+3+at;parent[at?at:1]=0;
    FatEnt *entries=api->kmalloc(129*sizeof(FatEnt));if(!entries)return -1;
    int count=api->fat_list(parent,entries,129),found=0;
    if(count<0)found=-1;
    else for(int i=0;i<count&&i<129;i++)if(!api->strcasecmp(entries[i].name,base)){
        *size=entries[i].size;*stamp=entries[i].mtime;*dir=entries[i].is_dir;found=1;break;
    }
    if(!found&&count>=129)found=-1;
    api->kfree(entries);return found;
}
static int managed_parent(const char *p)
{
    char parent[256];api->strlcpy(parent,p,sizeof parent);int at=2;
    for(int i=3;parent[i];i++)if(parent[i]=='/')at=i;
    parent[at==2?3:at]=0;u32 size,stamp;int dir;
    return managed_stat(parent,&size,&stamp,&dir)==1&&dir;
}
static int managed_read(const char *p,u8 *buf,u32 n)
{
    return p[0]=='a'?api->fs_read(p+3,buf,n):api->fat_read(p+2,buf,n);
}
static void managed_blob(const u8 *buf,u32 size)
{
    char b[64];api->kfmt(b,sizeof b,"@file %u %08x\r\n",size,remote_crc(buf,size));text(b);
    for(u32 i=0;i<size&&!io_failed;i+=512){
        u32 take=size-i;if(take>512)take=512;hexline("@data ",buf+i,(int)take);
    }
    done(0);
}
static void managed_file(const char *p,int hash)
{
    u32 size,stamp,generation=api->usb_gen();int dir;
    int found=managed_stat(p,&size,&stamp,&dir);
    if(found!=1||dir){error(found<0?"Cannot read directory; check the disk":"File not found");return;}
    if(size>FS_MAXFILE){error("File exceeds the 512 KiB transfer limit");return;}
    u8 *buf=api->kmalloc(size?size:1);
    if(!buf){error("Not enough memory");return;}
    int n=managed_read(p,buf,size);
    if(n!=(int)size||(p[0]=='u'&&generation!=api->usb_gen())){
        api->kfree(buf);error("File read failed or USB changed; retry");return;
    }
    if(hash){
        mj("{\"size\":");mj_number(size);mj_value("crc32",remote_crc(buf,size));
        mj_value("modified",stamp);mj("}");done(0);
    }else managed_blob(buf,size);
    api->kfree(buf);
}
static void managed_row(const char *name,u32 size,u32 stamp,int dir,int *comma)
{
    if((*comma)++)mj(",");
    mj("{\"name\":");mj_string(name);mj_value("size",size);
    mj_value("modified",stamp);mj_value("directory",!!dir);mj("}");
}
static void managed_list(const char *p)
{
    u32 size,stamp;int dir,comma=0;
    if(managed_stat(p,&size,&stamp,&dir)!=1||!dir){error("Folder not found or unreadable");return;}
    if(p[0]=='a'){
        FsEnt *rows=api->kmalloc(FS_NFILES*sizeof(FsEnt));
        if(!rows){error("Not enough memory");return;}
        int count=0;u32 len=api->strlen(p+3);
        for(int i=0;i<FS_NFILES;i++){
            FsEnt *e=api->fs_slot(i);if(!e||!e->used)continue;
            const char *name=e->name;
            if(len){if(api->strncmp(name,p+3,len)||name[len]!='/')continue;name+=len+1;}
            if(!*name)continue;
            char child[FS_NAMELEN];int j=0;
            while(name[j]&&name[j]!='/'){child[j]=name[j];j++;}child[j]=0;
            int duplicate=0;for(int k=0;k<count;k++)if(!api->strcmp(rows[k].name,child)){duplicate=1;break;}
            if(duplicate)continue;
            api->memcpy(&rows[count],e,sizeof *e);api->strlcpy(rows[count].name,child,FS_NAMELEN);
            if(name[j]=='/'){rows[count].attr|=FS_ATTR_DIR;rows[count].size=0;}
            count++;
        }
        mj("{\"entries\":[");
        for(int i=0;i<count;i++)managed_row(rows[i].name,rows[i].size,rows[i].mtime,rows[i].attr&FS_ATTR_DIR,&comma);
        api->kfree(rows);mj("],\"truncated\":false}");done(0);
    }else{
        FatEnt *rows=api->kmalloc(129*sizeof(FatEnt));
        if(!rows){error("Not enough memory");return;}
        int n=api->fat_list(p+2,rows,129);
        if(n<0){api->kfree(rows);error("USB folder read failed");return;}
        mj("{\"entries\":[");
        for(int i=0;i<n&&i<128;i++)if(api->strcmp(rows[i].name,".")&&api->strcmp(rows[i].name,".."))
            managed_row(rows[i].name,rows[i].size,rows[i].mtime,rows[i].is_dir,&comma);
        api->kfree(rows);mj(n>128?"],\"truncated\":true}":"],\"truncated\":false}");done(0);
    }
}
static int managed_matches(const char *p,u32 expected,u32 crc)
{
    u32 size,stamp;int dir;
    int found=managed_stat(p,&size,&stamp,&dir);
    if(expected==~0u)return found==0;
    if(found!=1||dir||size!=expected||size>FS_MAXFILE)return 0;
    u8 *buf=api->kmalloc(size?size:1);if(!buf)return 0;
    int ok=managed_read(p,buf,size)==(int)size&&remote_crc(buf,size)==crc;
    api->kfree(buf);return ok;
}
static void managed_put(const char *args)
{
    upload_clear();
    if(!managed_path(&args,target)||!target[3]||!number(&args,&upload_size,10)||
       !number(&args,&upload_crc,16)||!number(&args,&managed_expected_size,10)||
       !number(&args,&managed_expected_crc,16)||*args||upload_size>FS_MAXFILE||
       (managed_expected_size!=~0u&&managed_expected_size>FS_MAXFILE)||
       (managed_expected_size==~0u&&managed_expected_crc)){
        upload_clear();error("Invalid file upload request");return;
    }
    if(managed_protected(target)&&!path_ok(target+3)){
        upload_clear();error("Use a validated sys/name.kx extension for system files");return;
    }
    if(!managed_parent(target)){upload_clear();error("Parent folder not found or unreadable");return;}
    if(target[0]=='u'&&!api->fat_writable()){upload_clear();error("USB is read-only");return;}
    managed_usb_gen=api->usb_gen();
    upload=api->kmalloc(upload_size?upload_size:1);
    if(!upload){upload_clear();error("Not enough memory");return;}
    upload_managed=1;text("@ready\r\n");done(0);
}
static void managed_commit(void)
{
    if(!upload||upload_got!=upload_size||remote_crc(upload,upload_size)!=upload_crc){
        upload_clear();error("Incomplete upload or checksum mismatch; file unchanged");return;
    }
    u32 len=api->strlen(target);
    if(len>=3&&!api->strcasecmp(target+len-3,".kx")){
        KextHeader header;int r=files->check(upload,upload_size,&header);
        if(r){upload_clear();error(r==43?"Kext needs a newer kernel API; file unchanged":"Invalid kext image; file unchanged");return;}
    }
    if((target[0]=='u'&&api->usb_gen()!=managed_usb_gen)||
       !managed_matches(target,managed_expected_size,managed_expected_crc)){
        upload_clear();error("Target changed or cannot be checked; refresh and retry");return;
    }
    u8 *check=api->kmalloc(upload_size?upload_size:1);
    if(!check){upload_clear();error("Not enough memory for verification; file unchanged");return;}
    if(target[0]=='u'&&api->usb_gen()!=managed_usb_gen){
        api->kfree(check);upload_clear();error("USB changed; file unchanged");return;
    }
    int r=target[0]=='a'?files->replace(target+3,upload,upload_size):api->fat_write(target+2,upload,upload_size);
    if(!r)r=managed_read(target,check,upload_size)!=(int)upload_size||remote_crc(check,upload_size)!=upload_crc?-4:0;
    if(target[0]=='u'&&api->usb_gen()!=managed_usb_gen)r=-4;
    api->kfree(check);
    if(!r){text("@saved ");text(target);text(" - verified\r\n");api->broadcast("fs.changed",target);}
    upload_clear();
    if(r)error(r==-2?"Disk full; free space and retry":r==-4?"Readback failed; verify the target before retrying":"Disk write failed; check the target and any PC backup");
    else done(0);
}
static void managed_mutate(const char *action,const char *args)
{
    char p[256],dest[256];u32 size,stamp,device_gen=api->usb_gen();int dir;
    if(!managed_path(&args,p)||!p[3]||managed_protected(p)){error("Invalid or protected system path");return;}
    int found=managed_stat(p,&size,&stamp,&dir),r=-1;
    if(found<0){error("Cannot read directory; check the disk");return;}
    if(!api->strcmp(action,"mkdir")){
        if(*args||found||!managed_parent(p)){error("Folder exists or its parent is unavailable");return;}
        if(p[0]=='u'&&device_gen!=api->usb_gen()){error("USB changed; refresh and retry");return;}
        r=p[0]=='a'?api->fs_mkdir(p+3):api->fat_mkdir(p+2);
    }else if(!api->strcmp(action,"rename")){
        if(!found||!managed_path(&args,dest)||*args||!dest[3]||p[0]!=dest[0]||
           managed_protected(dest)||!managed_parent(dest)){error("Invalid rename or unavailable parent");return;}
        u32 ds,dt;int dd;
        if(managed_stat(dest,&ds,&dt,&dd)!=0){error("Destination exists or cannot be checked");return;}
        if(p[0]=='a')r=dir?api->fs_rename_dir(p+3,dest+3):api->fs_rename(p+3,dest+3);
        else{
            int a=2,b=2;for(int i=3;p[i];i++)if(p[i]=='/')a=i;
            for(int i=3;dest[i];i++)if(dest[i]=='/')b=i;
            if(a!=b||api->strncmp(p,dest,a)){error("USB rename must stay in the same folder");return;}
            if(device_gen!=api->usb_gen()){error("USB changed; refresh and retry");return;}
            r=api->fat_rename(p+2,dest+b+1);
        }
    }else{
        u32 expected,crc;
        if(!found||!number(&args,&expected,10)||!number(&args,&crc,16)||*args){error("Invalid delete request");return;}
        if(dir){
            if(expected!=~0u||crc){error("Folder changed; refresh and retry");return;}
            if(p[0]=='a'){
                if(api->fs_dir_count(p+3)){error("Folder is not empty");return;}
                r=api->fs_delete(p+3);
            }else{
                if(device_gen!=api->usb_gen()){error("USB changed; refresh and retry");return;}
                r=api->fat_rmdir(p+2);
            }
        }else{
            if(!managed_matches(p,expected,crc)){error("File changed since backup; refresh and retry");return;}
            if(p[0]=='u'&&device_gen!=api->usb_gen()){error("USB changed; refresh and retry");return;}
            r=p[0]=='a'?api->fs_delete(p+3):api->fat_delete(p+2);
        }
    }
    if(r){error(r==-2?"Disk is full or unavailable":r==-3?"Folder is not empty":"File operation failed; refresh to check the disk");return;}
    api->broadcast("fs.changed",p);mj("{\"ok\":true}");done(0);
}
static void managed_info(void)
{
    mj("{\"protocol\":1");mj_text("os",api->os_version);mj_text("build",api->os_build_date);
    mj_value("api",api->version);mj_value("uptime_seconds",ticks/100);
    mj_text("cpu",api->cpu_brand());mj_value("cpu_mhz",api->cpu_mhz());
    mj_value("system_idle_per_mille",api->cpu_usage(-1));
    mj_value("memory_kb",api->mem_total_kb());mj_value("used_kb",api->mem_used_kb());
    mj_value("heap_free",api->heap_avail());mj_value("heap_largest",api->mem_info(MI_HEAP_LARGEST));
    mj_value("floppy_free_kb",api->fs_free_kb());mj_value("usb_present",api->usb_present());
    mj_value("usb_capacity_kb",api->usb_capacity_kb());
    mj_value("screen_width",*api->screen_w);mj_value("screen_height",*api->screen_h);
    mj_ip("ip",NET_IP);mj_ip("mask",NET_MASK);mj_ip("gateway",NET_GW);mj_ip("dns",NET_DNS);
    mj_value("link",api->net_get(NET_LINK));mj_value("received",api->net_get(NET_RX));
    mj_value("transmitted",api->net_get(NET_TX));mj_value("dhcp",api->net_get(NET_DHCP_OK));
    mj_value("lease_seconds",api->net_get(NET_LEASE_LEFT));
    mj_value("disk_operations",api->disk_stat(DS_OPS));mj_value("disk_retries",api->disk_stat(DS_RETRIED));
    mj_value("disk_failures",api->disk_stat(DS_FAILED));mj_value("faults",api->fault_count());
    mj("}");done(0);
}
static void managed_apps(void)
{
    int comma=0;mj("{\"apps\":[");
    for(int i=0;i<api->app_count()&&i<128;i++){
        const AppDesc *d=api->app_desc(i);if(!d||!d->in_menu)continue;
        char title[96];api->strlcpy(title,d->title,sizeof title);
        if(comma++)mj(",");mj("{\"id\":");mj_number(i);mj_text("title",title);
        mj_value("cpu_per_mille",api->cpu_usage(i));mj("}");
    }
    mj("],\"windows\":[");comma=0;
    for(int i=0;i<api->win_max();i++){
        const Win *w=api->win_slot(i);if(!w||!w->used)continue;
        int type=w->type,inst=w->inst;char title[96];
        api->strlcpy(title,w->tbuf_on?w->tbuf:w->title,sizeof title);
        if(comma++)mj(",");mj("{\"id\":");mj_number(i);mj_value("type",type);mj_value("instance",inst);
        mj_text("title",title);mj_value("cpu_per_mille",api->cpu_usage(type));mj("}");
    }
    mj("],\"extensions\":[");comma=0;
    for(int i=0;i<api->kext_count();i++){
        const KextInfo *k=api->kext_get(i);if(!k)continue;
        KextInfo saved;api->memcpy(&saved,k,sizeof saved);
        if(comma++)mj(",");mj("{\"id\":");mj_number(i);mj_text("path",saved.name);
        mj_text("title",saved.hname);mj_value("kind",saved.kind);mj_value("bytes",saved.size);
        mj(",\"status\":");mj_signed(saved.status);mj("}");
    }
    mj("]}");done(0);
}
static void managed_app_action(const char *action,const char *args)
{
    u32 id,type,inst;
    if(!number(&args,&id,10)||id>127){error("Invalid app or window");return;}
    if(!api->strcmp(action,"launch")){
        const AppDesc *d=api->app_desc((int)id);
        if(*args||!d||!d->in_menu||api->win_open((int)id)<0){error("Cannot open app");return;}
    }else{
        if(!number(&args,&type,10)||!number(&args,&inst,10)||*args||id>=(u32)api->win_max()){
            error("Invalid window");return;
        }
        const Win *w=api->win_slot((int)id);
        if(!w||!w->used||w->type!=type||w->inst!=inst){error("Window changed; refresh and retry");return;}
        if(!api->strcmp(action,"focus"))api->win_focus((int)type,(int)inst);
        else{
            const ManagerOps *core=api->service_get("manager.core");
            if(!core||core->abi!=MANAGER_ABI){error("Update the kernel to request a normal app close");return;}
            if(!core->close_request((int)id)){error("Window is already closed");return;}
        }
    }
    mj("{\"ok\":true}");done(0);
}
static void managed_diagnostics(void)
{
    char *buf=api->kmalloc(16384);
    if(!buf){error("Not enough memory");return;}
    int n=api->klog_read(buf,16384);if(n<0)n=0;if(n>16383)n=16383;buf[n]=0;
    mj("{\"log\":");mj_string(buf);
    const DebugOps *debug=api->service_get("debug");
    buf[0]=0;if(debug&&debug->abi==DEBUG_ABI&&debug->snapshot)debug->snapshot(buf,16384);
    buf[16383]=0;mj_text("debug",buf);api->kfree(buf);
    mj(",\"faults\":[");int comma=0;
    for(int i=0;i<api->fault_count();i++){
        const FaultRec *r=api->fault_get(i);if(!r)continue;
        FaultRec f;api->memcpy(&f,r,sizeof f);
        if(comma++)mj(",");mj("{\"sequence\":");mj_number(f.sequence);
        mj_value("vector",f.vec);mj_value("error",f.err);mj_value("address",f.eip);
        mj_value("tick",f.tick);mj_value("cr2",f.cr2);mj_text("owner",f.owner);
        mj_text("location",f.location);mj_text("module",f.module);mj("}");
    }
    mj("]}");done(0);
}
static void managed_u32(u8 *b,u32 n){b[0]=(u8)n;b[1]=(u8)(n>>8);b[2]=(u8)(n>>16);b[3]=(u8)(n>>24);}
static void managed_screen(void)
{
    int width=*api->screen_w,height=*api->screen_h,scale=1;
    if(width<1||height<1||width>4096||height>4096){error("Screen unavailable");return;}
    while((u32)((width/scale+3)&~3)*(u32)(height/scale)>FS_MAXFILE-1078)scale++;
    int w=width/scale,h=height/scale,stride=(w+3)&~3;u32 size=1078+(u32)stride*(u32)h;
    u8 *buf=api->kmalloc(size);
    if(!buf){error("Not enough memory for screenshot");return;}
    api->memset(buf,0,size);buf[0]='B';buf[1]='M';managed_u32(buf+2,size);
    managed_u32(buf+10,1078);managed_u32(buf+14,40);managed_u32(buf+18,w);
    managed_u32(buf+22,h);buf[26]=1;buf[28]=8;managed_u32(buf+34,size-1078);managed_u32(buf+46,256);
    for(int i=0;i<256;i++){u8 r,g,b;api->palette_rgb(i,&r,&g,&b);buf[54+i*4]=b;buf[55+i*4]=g;buf[56+i*4]=r;}
    for(int y=0;y<h;y++){
        u8 *row=buf+1078+(h-1-y)*stride;
        if(scale==1)api->read_rect(0,y,w,1,row,stride);
        else for(int x=0;x<w;x++)row[x]=api->getpixel(x*scale,y*scale);
    }
    managed_blob(buf,size);api->kfree(buf);
}
#define FRAME_CHUNK 4096
static u32 *frame_hash,frame_pal;
static u8 *frame_out,frame_map[256],frame_rgb[768];
static int frame_w,frame_h;
static void frame_reset(void)
{
    api->kfree(frame_hash);api->kfree(frame_out);
    frame_hash=0;frame_out=0;frame_w=frame_h=0;
}
static void managed_frame(const char *args)
{
    u32 delta;
    if(!number(&args,&delta,10)||*args||delta>1){error("Invalid frame request");return;}
    u8 *screen=0;int w=0,h=0,pitch=0;
    if(!api->surface_lock(&screen,&pitch,&w,&h)||!screen||w<1||h<1||w>1024||h>1024||pitch<w){
        if(screen)api->surface_unlock();
        error("Screen unavailable");return;
    }
    int tw=(w+ST_TILE-1)/ST_TILE,th=(h+ST_TILE-1)/ST_TILE;
    if(w!=frame_w||h!=frame_h||!frame_hash){
        frame_reset();delta=0;
        frame_hash=api->kmalloc((u32)(tw*th)*4);frame_out=api->kmalloc(FRAME_CHUNK+ST_RECORD+800);
        if(!frame_hash||!frame_out){frame_reset();api->surface_unlock();error("Not enough memory for the screen view");return;}
        frame_w=w;frame_h=h;
    }
    for(int i=0;i<256;i++)api->palette_rgb(i,frame_rgb+i*3,frame_rgb+i*3+1,frame_rgb+i*3+2);
    u32 palette=st_hash(frame_rgb,768,768,1);
    int flags=(delta?0:ST_KEYFRAME)|(!delta||palette!=frame_pal?ST_PALETTE:0);
    frame_pal=palette;
    int used=st_header(frame_out,w,h,*api->mouse_x,*api->mouse_y,flags);
    if(flags&ST_PALETTE){api->memcpy(frame_out+used,frame_rgb,768);used+=768;}
    for(int ty=0;ty<th&&!io_failed;ty++){
        int y=ty*ST_TILE,rows=h-y<ST_TILE?h-y:ST_TILE;
        for(int tx=0;tx<tw;tx++){
            int x=tx*ST_TILE,cols=w-x<ST_TILE?w-x:ST_TILE,t=ty*tw+tx;
            const u8 *tile=screen+(u32)y*(u32)pitch+(u32)x;
            if(used>FRAME_CHUNK){binary(frame_out,used);used=0;}
            u32 hash=st_hash(tile,pitch,cols,rows);
            if(delta&&hash==frame_hash[t])continue;
            frame_hash[t]=hash;
            used+=st_record(frame_out+used,t,tile,pitch,cols,rows,frame_map);
        }
    }
    api->surface_unlock();
    used+=st_end(frame_out+used);binary(frame_out,used);done(0);
}
static const InputOps *input_core(void)
{
    const InputOps *core=api->service_get("input.core");
    return core&&core->abi==INPUT_ABI&&core->post?core:0;
}
static int input_send(const InputOps *core,int mouse,u32 value)
{
    for(int tries=0;tries<50;tries++){if(core->post(mouse,value))return 1;api->gui_pump();}
    return 0;
}
static void managed_pointer(const char *args)
{
    u32 x,y,buttons,wheel;const InputOps *core=input_core();
    if(!core){error("Update the kernel to control this screen");return;}
    if(!number(&args,&x,10)||!number(&args,&y,10)||!number(&args,&buttons,10)||!number(&args,&wheel,10)||
       *args||x>1023||y>1023||buttons>7||wheel>255){error("Invalid pointer request");return;}
    if(!input_send(core,1,input_pointer((int)x,(int)y,(int)buttons,(int)wheel))){error("Input queue full; retry");return;}
    mj("{\"ok\":true}");done(0);
}
static void managed_keys(const char *args)
{
    const InputOps *core=input_core();int n=(int)api->strlen(args);
    if(!core){error("Update the kernel to control this screen");return;}
    if(!n||(n&1)||n>64){error("Invalid key request");return;}
    for(int i=0;i<n;i++)if(digit(args[i])<0){error("Invalid key request");return;}
    for(int i=0;i<n;i+=2)
        if(!input_send(core,0,(u32)(digit(args[i])<<4|digit(args[i+1])))){error("Input queue full; retry");return;}
    mj("{\"ok\":true}");done(0);
}
static void managed_command(const char *args)
{
    char action[24];int n=0;
    while(*args&&*args!=' '){if(n==23){error("Unknown management request");return;}action[n++]=*args++;}
    action[n]=0;while(*args==' ')args++;
    if(upload){error("Upload active; commit or abort first");return;}
    if(!api->strcmp(action,"capabilities")&&!*args){
        const ManagerOps *core=api->service_get("manager.core");
        mj("{\"protocol\":1,\"max_file\":524288,\"files\":true,\"apps\":true,\"diagnostics\":true,\"screenshot\":true,\"close\":");
        mj(core&&core->abi==MANAGER_ABI?"true":"false");
        mj(",\"screen\":true,\"control\":");mj(input_core()?"true}":"false}");done(0);
    }else if(!api->strcmp(action,"info")&&!*args)managed_info();
    else if(!api->strcmp(action,"apps")&&!*args)managed_apps();
    else if(!api->strcmp(action,"diagnostics")&&!*args)managed_diagnostics();
    else if(!api->strcmp(action,"screenshot")&&!*args)managed_screen();
    else if(!api->strcmp(action,"frame"))managed_frame(args);
    else if(!api->strcmp(action,"pointer"))managed_pointer(args);
    else if(!api->strcmp(action,"keys"))managed_keys(args);
    else if(!api->strcmp(action,"put"))managed_put(args);
    else if(!api->strcmp(action,"mkdir")||!api->strcmp(action,"rename")||!api->strcmp(action,"delete"))managed_mutate(action,args);
    else if(!api->strcmp(action,"launch")||!api->strcmp(action,"focus")||!api->strcmp(action,"close"))managed_app_action(action,args);
    else if(!api->strcmp(action,"list")||!api->strcmp(action,"get")||!api->strcmp(action,"hash")){
        char path[256];
        if(!managed_path(&args,path)||*args){error("Invalid file path");return;}
        if(!api->strcmp(action,"list"))managed_list(path);
        else managed_file(path,!api->strcmp(action,"hash"));
    }else error("Unknown management request");
}
