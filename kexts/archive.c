/* Creates and extracts archives using private file buffers. */
#include "kapi.h"
#include "gdi.h"
#include "archive_core.inc"
#include "filepaths.inc"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
static u8 *data;
static ArEntry entries[AR_FILES];
static u32 length=8;
static u32 allocated;
static int count,selected,scroll,dirty,type=-1,alive,extract_all,focus;
static int working,pending;
static char pending_path[202];
static void run_pending(void);
static char filename[FS_NAMELEN+2],message[100];static AppField field;
static void say(const char *s){api->strlcpy(message,s,sizeof message);api->gui_dirty();}
static int reserve(void)
{
    if(!data){allocated=64;data=api->kmalloc(allocated+1);if(data){ar_empty(data);if(api->mem_track)api->mem_track("Archive buffer",data,allocated+1);}}
    if(!data){say("Not enough memory. Close another app and try again.");return 0;}return 1;
}
static int grow(u32 wanted)
{
    if(wanted>AR_CAP)wanted=AR_CAP;
    if(wanted<=allocated)return 1;
    u8 *next=api->krealloc(data,wanted+1);
    if(!next){
        u32 room=api->mem_info(MI_HEAP_LARGEST);
        if(room<=4097)return 0;
        if(wanted>room-4097)wanted=room-4097;
        if(wanted<=allocated)return 0;
        next=api->krealloc(data,wanted+1);if(!next)return 0;
    }
    data=next;allocated=wanted;if(api->mem_track)api->mem_track("Archive buffer",data,allocated+1);return 1;
}
static void clear(void)
{
    if(data)api->kfree(data);data=0;allocated=0;length=8;count=selected=scroll=dirty=0;
    af_set(&field,filename,sizeof filename,"desktop/files.fpa");focus=0;
    say("Drop files here or choose Add, then Save the archive.");
}
static int validate(void)
{
    count=ar_index(data,length,entries);if(count<0)return 0;
    u32 needed=1;for(int i=0;i<count;i++)if(entries[i].raw>needed)needed=entries[i].raw;
    u8 *raw=api->kmalloc(needed);if(!raw){say("Not enough memory to check this archive.");return -1;}
    if(api->mem_track)api->mem_track("Archive workspace",raw,needed);
    int ok=1;for(int i=0;i<count;i++)if(lz_unpack(data+entries[i].offset,entries[i].packed,raw,needed)!=(int)entries[i].raw){ok=0;break;}
    api->kfree(raw);return ok;
}
static void opened(int i)
{
    (void)i;alive=1;pending=0;
    if(!data)clear();else say(dirty?"Your unsaved archive is still here. Save it to keep it.":"Select a file to extract, or add more files.");
}
static void closed(int i)
{
    (void)i;alive=0;if(working)return;
    if(!dirty){clear();return;}
    pending=0;
}
static void work_end(void){working=0;if(!alive)closed(0);}
static const char *leaf(const char *s){const char *b=s;for(;*s;s++)if(*s=='/'||*s==':')b=s+1;return b;}
static int read_path(const char *p,u8 *b,u32 cap){if((p[0]=='u'||p[0]=='U')&&p[1]==':')return api->fat_read(p+2,b,cap);if((p[0]=='a'||p[0]=='A')&&p[1]==':')p+=2;return api->fs_read(p,b,cap);}
static int path_size(const char *p,u32 *size)
{
    if((p[0]=='u'||p[0]=='U')&&p[1]==':'){
        p+=2;const char *b=leaf(p);int n=(int)(b-p);char dir[200];
        if(n>=(int)sizeof dir)return 0;
        if(n>1)n--;if(!n){dir[0]='/';n=1;}else api->memcpy(dir,p,n);dir[n]=0;
        FatEnt *items=api->kmalloc(128*sizeof *items);if(!items)return 0;
        int count=api->fat_list(dir,items,128),found=0;
        for(int i=0;i<count;i++)if(!items[i].is_dir&&!api->strcasecmp(items[i].name,b)){*size=items[i].size;found=1;break;}
        api->kfree(items);return found;
    }
    if((p[0]=='a'||p[0]=='A')&&p[1]==':')p+=2;
    for(int i=0;i<FS_NFILES;i++){FsEnt *e=api->fs_slot(i);if(e&&e->used&&!(e->attr&FS_ATTR_DIR)&&!api->strcmp(e->name,p)){*size=e->size;return 1;}}
    return 0;
}
static void load_file(const char *p)
{
    if(!alive||!p||!reserve())return;
    u32 expected;
    if(!path_size(p,&expected)||expected>AR_CAP){say("Archive is unreadable or too large for this system.");return;}
    u32 needed=expected<=AR_CAP-36?expected+36:expected;
    u8 *previous=data;u32 previous_allocated=allocated,previous_length=length;
    int previous_count=count;
    u8 *next=api->kmalloc(needed+1);
    if(!next){say("Not enough memory to open this archive. Close another app.");return;}
    data=next;allocated=needed;
    if(api->mem_track)api->mem_track("Archive buffer",data,allocated+1);
    api->busy_set("Archive Manager","Reading and checking archive...",-1);
    int n=read_path(p,data,expected+1),ok=0;
    if(n!=(int)expected)n=-1;
    if(n>=LZ_HDR&&data[0]=='P'&&data[1]=='Z'&&data[2]=='1'&&!data[3]){

        const char *b=leaf(p);int len=(int)api->strlen(b);
        if(len>3&&len-3<24&&(u32)n<=allocated-36){
            api->memmove(data+36,data,(u32)n);ar_empty(data);api->memset(data+8,0,28);
            api->memcpy(data+8,b,(u32)len-3);data[4]=1;lz_put32(data+32,(u32)n);n+=36;
        }else n=-1;
    }
    if(n>=8&&n<=(int)AR_CAP){length=(u32)n;ok=validate();}
    api->busy_end();
    if(ok!=1){
        api->kfree(data);data=previous;allocated=previous_allocated;length=previous_length;count=previous_count;
        ar_index(data,length,entries);
        say(ok<0?"Not enough memory to check this archive.":"Could not open archive. The current archive was kept.");return;
    }
    api->kfree(previous);
    selected=scroll=dirty=0;af_set(&field,filename,sizeof filename,"desktop/files.fpa");
    const char *local=p;if((p[0]=='a'||p[0]=='A')&&p[1]==':')local+=2;
    int len=(int)api->strlen(local);
    if(!((p[0]=='u'||p[0]=='U')&&p[1]==':')&&len<24&&len>4&&!api->strcasecmp(local+len-4,".fpa"))af_set(&field,filename,sizeof filename,local);
    say("Archive checked. Select a file or extract them all.");
}
static void load_path(const char *p)
{
    if(working)return;working=1;load_file(p);work_end();
}
static void picked_open(const char *p,void *ctx){(void)ctx;load_path(p);}
static void add_file(const char *p)
{
    if(!alive||!p||!reserve())return;
    const char *b=leaf(p);if(!ar_name(b)){say("Use a file name of 1-23 simple characters.");return;}
    if(count>=AR_FILES){say("This archive already has 32 files.");return;}
    for(int i=0;i<count;i++)if(ar_same(entries[i].name,b)){say("That file name is already in the archive.");return;}
    u32 expected;
    if(!path_size(p,&expected)){say("Could not find or read that file.");return;}
    if(expected>AR_CAP||length>AR_CAP-28-LZ_HDR){say("File or archive is too large for this system.");return;}
    char name[24];api->strlcpy(name,b,sizeof name);
    u8 *raw=api->kmalloc(expected+1);if(!raw){say("Not enough memory to add a file. Close another app.");return;}
    if(api->mem_track)api->mem_track("Archive workspace",raw,expected+1);
    api->busy_set("Archive Manager","Reading and compressing file...",-1);
    int n=read_path(p,raw,expected+1);u32 packed=0;
    if(n!=(int)expected)n=-1;
    if(n>=0){
        u32 room=AR_CAP-length-28-LZ_HDR;
        grow((u32)n>room?AR_CAP:length+28+LZ_HDR+(u32)n);
        if(length+28<allocated)packed=lz_pack(raw,(u32)n,data+length+28,allocated-length-28);
    }
    api->kfree(raw);api->busy_end();
    if(!packed){say(n<0?"Could not read the complete file.":"Not enough memory to add this file. Close another app.");return;}
    api->memset(data+length,0,24);api->strlcpy((char *)data+length,name,24);lz_put32(data+length+24,packed);
    length+=28+packed;data[4]=(u8)++count;ar_index(data,length,entries);selected=count-1;dirty=1;
    say("File added. Save the archive when you are ready.");
}
static void add_path(const char *p,void *ctx)
{
    (void)ctx;if(working)return;working=1;add_file(p);work_end();
}
static void dropped(int inst,int x,int y,const char *kind,const char *payload)
{
    (void)inst;(void)x;(void)y;
    if(!alive||!kind||api->strcmp(kind,"file")||!payload||!payload[0])return;
    if(working){api->notify("Archive Manager is busy. Try dropping the files again when it finishes.");return;}
    int n=0;while(n<4096&&payload[n])n++;
    if(n==4096){say("Too many file paths. Drop fewer files at a time.");return;}
    working=1;
    char *list=api->kmalloc((u32)n+1);
    if(!list){say("Not enough memory to add files. Close another app.");work_end();return;}
    api->memcpy(list,payload,(u32)n+1);
    int added=0,skipped=0;char reason[64];reason[0]=0;
    for(char *p=list;*p&&alive;){
        char *next=p;while(*next&&*next!='\n')next++;
        if(*next)*next++=0;
        int len=(int)api->strlen(p),before=count;
        if(!len){p=next;continue;}
        if(len>=200)say("A file path is too long.");
        else if(p[len-1]=='/')say("Open the folder and drop its files instead.");
        else if(len<3||p[1]!=':'||!(p[0]=='a'||p[0]=='A'||p[0]=='u'||p[0]=='U'))say("That file location is not supported.");
        else add_file(p);
        if(count>before)added++;
        else {skipped++;if(!reason[0])api->strlcpy(reason,message,sizeof reason);}
        p=next;
    }
    api->kfree(list);
    if(added+skipped>1){
        if(skipped)api->kfmt(message,sizeof message,"%d added; %d skipped. %s",added,skipped,reason);
        else api->kfmt(message,sizeof message,"%d files added. Save the archive when you are ready.",added);
        api->gui_dirty();
    }
    work_end();
}
static int save_name(char out[FS_NAMELEN])
{
    const char *s=filename;if((s[0]=='a'||s[0]=='A')&&s[1]==':')s+=2;
    int n=(int)api->strlen(s);if(n<5||n>=FS_NAMELEN||api->strcasecmp(s+n-4,".fpa"))return 0;
    char part[FS_NAMELEN];int k=0;
    for(int i=0;i<=n;i++){
        if(!s[i]||s[i]=='/'){part[k]=0;if(!k||(k==1&&part[0]=='.')||(k==2&&part[0]=='.'&&part[1]=='.'))return 0;k=0;}
        else {if((u8)s[i]<32||s[i]==':'||s[i]=='\\')return 0;part[k++]=s[i];}
    }
    api->strlcpy(out,s,FS_NAMELEN);return 1;
}
static void save_file(int answer,void *ctx)
{
    (void)ctx;if(!alive||answer!=MBR_YES)return;
    char name[FS_NAMELEN];if(!save_name(name)||!reserve())return;
    api->busy_set("Archive Manager","Writing archive...",-1);
    int r=api->fs_write(name,data,length);api->busy_end();
    if(r==0){dirty=0;say("Archive saved.");api->broadcast("file.saved",name);}
    else say(r==-2?"Not enough contiguous disk space. Try another disk or free space.":"Archive could not be saved. Your changes are still here.");
}
static void save_now(int answer,void *ctx)
{
    if(working)return;working=1;save_file(answer,ctx);work_end();
    if(answer==MBR_YES&&!dirty)run_pending();else pending=0;
}
static void save(void)
{
    char name[FS_NAMELEN];if(!save_name(name)){pending=0;say("Use an A: name ending in .fpa (63 characters including folder).");return;}
    if(api->fs_exists(name))api->msgbox("Replace archive?","A file with this name already exists. Replace it?",MB_YESNO,save_now,0);
    else save_now(MBR_YES,0);
}
static void extract_files(const char *p,void *ctx)
{
    (void)ctx;if(!alive||!p||!data||!count)return;
    int usb=(p[0]=='u'||p[0]=='U')&&p[1]==':';
    if(!usb&&!((p[0]=='a'||p[0]=='A')&&p[1]==':')){say("Choose a folder on A: or U: for extraction.");return;}
    if(usb&&(!api->usb_present()||!api->fat_writable())){say("USB is missing or read-only.");return;}
    const char *source=p+2;if(!usb)while(*source=='/')source++;
    int fl=(int)api->strlen(source);while(fl&&source[fl-1]=='/')fl--;
    char folder[64],path[FT_PATHMAX];
    if(fl>=(int)sizeof folder){say("Folder path is too long. Choose a shorter path.");return;}
    api->memcpy(folder,source,(u32)fl);folder[fl]=0;
    if(usb&&!fl)api->strlcpy(folder,"/",sizeof folder);
    int first=extract_all?0:selected,last=extract_all?count:selected+1;

    u32 sectors=0;int slots=0;
    for(int i=first;i<last;i++){
        if(ft_join(api,usb,folder,entries[i].name,path)){say("A file path is too long. Choose a shorter folder path.");return;}
        if(ft_exists(api,usb,path)){say("A destination file already exists. Choose an empty folder.");return;}
        if(usb){int rc=ft_usb_name(api,folder,entries[i].name,path);if(rc){say(ft_reason(rc));return;}}
        sectors+=(entries[i].raw+511)/512;
    }
    if(!usb){
        for(int i=0;i<FS_NFILES;i++){FsEnt *e=api->fs_slot(i);if(e&&!e->used)slots++;}
        if(slots<last-first||sectors>api->fs_free_kb()*2){say("There is not enough free space for these files.");return;}
    }
    u32 needed=1;for(int i=first;i<last;i++)if(entries[i].raw>needed)needed=entries[i].raw;
    u8 *raw=api->kmalloc(needed);if(!raw){say("Not enough memory to extract files.");return;}
    if(api->mem_track)api->mem_track("Archive workspace",raw,needed);
    api->esc_arm();int done=0,error=0,renamed=0;

    for(int i=first;i<last;i++)if(lz_unpack(data+entries[i].offset,entries[i].packed,raw,needed)!=(int)entries[i].raw){error=1;break;}
    if(!error)for(int i=first;i<last;i++){
        if(api->esc_pending()){error=2;break;}
        api->busy_set("Extracting files",entries[i].name,(i-first)*256/(last-first));
        int n=lz_unpack(data+entries[i].offset,entries[i].packed,raw,needed);
        int rc=usb?ft_usb_name(api,folder,entries[i].name,path):ft_join(api,0,folder,entries[i].name,path);
        if(n<0||rc||ft_exists(api,usb,path)){error=1;break;}
        rc=usb?api->fat_write(path,raw,(u32)n):api->fs_write(path,raw,(u32)n);
        if(rc){error=rc==-2?3:1;break;}
        if(usb&&api->strcasecmp(leaf(path),entries[i].name))renamed=1;
        done++;
    }
    api->busy_end();api->kfree(raw);
    if(done)api->broadcast("file.changed","");
    api->kfmt(message,sizeof message,"%d file(s) extracted.%s",done,error==2?" Stopped; completed files were kept.":error==3?" Disk full; completed files were kept.":error?" Stopped after an error; completed files were kept.":renamed?" Long names shortened for USB.":"");
    api->gui_dirty();
}
static void extract_to(const char *p,void *ctx)
{
    if(working)return;working=1;extract_files(p,ctx);work_end();
}
static void remove_selected(void)
{
    if(!data||!count)return;u32 begin=entries[selected].offset-28,end=entries[selected].offset+entries[selected].packed;
    api->memmove(data+begin,data+end,length-end);length-=end-begin;data[4]=(u8)--count;ar_index(data,length,entries);
    if(selected>=count)selected=count?count-1:0;dirty=1;say("File removed from this archive. Save to keep the change.");
}
static void run_pending(void)
{
    int next=pending;pending=0;
    if(next==1)clear();
    else if(next==2)api->file_picker("Open archive (.fpa or .pz)",0,0,picked_open,0);
    else if(next==3){dirty=0;api->win_close_self(type,0);}
    else if(next==4)load_path(pending_path);
}
static void confirmed(int result,void *ctx)
{
    (void)ctx;if(!alive||working)return;
    if(result==MBR_YES)save();else if(result==MBR_NO)run_pending();else pending=0;
}
static void request_action(int next)
{
    if(working||pending)return;pending=next;
    if(dirty)api->msgbox("Unsaved changes","Save changes to this archive?",MB_SAVEDISCARD,confirmed,0);
    else run_pending();
}
static void action(int n)
{
    if(working)return;
    if(n<2)request_action(n+1);
    else if(n==2)api->file_picker("Add a file",0,0,add_path,0);
    else if(n==3)remove_selected();else if(n==4)save();
    else if(count){extract_all=n==6;api->file_picker("Extract to a folder on A: or U:",0,1,extract_to,0);}
}
static void size(int *w,int *h){*w=472;*h=304;}
static void initial(int i,int *w,int *h){(void)i;size(w,h);}
static int visible(int ch){int n=(ch-174)/18;return n>0?n:1;}
static void clamp(int ch){int v=visible(ch);if(scroll>count-v)scroll=count-v;if(scroll<0)scroll=0;if(selected<scroll)scroll=selected;if(selected>=scroll+v)scroll=selected-v+1;}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;clamp(ch);ui_header(x,y,cw,"Archive Manager");ui_header_right(x,y,cw,dirty?"Unsaved":"",C_MAROON);
    static const char *const labels[]={"New","Open...","Add...","Remove","Save"};
    for(int i=0;i<5;i++)ui_button(x,y,ui_r(12+i*78,34,72,22),labels[i],0,i!=3||count);
    api->draw_text(x+16,y+64,"File name",C_GRAY);api->draw_text(x+cw-164,y+64,"Original / Packed",C_GRAY);
    api->panel(x+12,y+82,cw-24,ch-174,1);api->fill_rect(x+14,y+84,cw-28,ch-178,C_WHITE);
    for(int r=0;r<visible(ch)&&scroll+r<count;r++){
        int i=scroll+r,yy=y+84+r*18;char t[40];
        if(i==selected)api->fill_rect(x+14,yy,cw-28,18,C_NAVY);
        u8 c=i==selected?C_WHITE:C_BLACK;api->draw_text_clip(x+18,yy+1,entries[i].name,c,cw-195);
        api->kfmt(t,sizeof t,"%u / %u",entries[i].raw,entries[i].packed);api->draw_text_clip(x+cw-164,yy+1,t,c,148);
    }
    if(!count)api->draw_text(x+24,y+94,"Drop files here or choose Add.",C_GRAY);
    api->draw_text(x+12,y+ch-82,"Save as",C_GRAY);af_draw(&field,x+78,y+ch-88,cw-90,focus);
    ui_button(x,y,ui_r(12,ch-56,144,24),"Extract selected",0,count>0);
    ui_button(x,y,ui_r(162,ch-56,112,24),"Extract all",0,count>0);
    char t[44];api->kfmt(t,sizeof t,"%u KiB",(length+1023)/1024);api->draw_text_clip(x+286,y+ch-51,t,C_GRAY,cw-298);
    ui_status(x,y,cw,ch,message);
}
static void key(int i,int k)
{
    (void)i;if(working||pending)return;if(k==K_CLOSE_REQUEST){request_action(3);return;}if(k==14){action(0);return;}if(k==15){action(1);return;}if(k==19){save();return;}if(k=='\t'){focus=!focus;return;}
    if(focus){af_key(&field,k);return;}
    if(k==K_UP&&selected)selected--;else if(k==K_DOWN&&selected+1<count)selected++;
    else if(k==K_DEL)remove_selected();else if(k=='\n')action(5);
}
static void mouse(int i,int x,int y,int ev,int cw,int ch)
{
    (void)i;if(working)return;ui_pointer(x,y,ev);
    if(af_mouse(&field,78,ch-88,cw-90,x,y,ev)){focus=1;return;}
    for(int n=0;n<5;n++)if(ui_click(ui_r(12+n*78,34,72,22),x,y,ev)){action(n);return;}
    if(ev==EV_PRESS){focus=ui_hit(ui_r(78,ch-88,cw-90,24),x,y);if(focus)return;}
    if(ui_click(ui_r(12,ch-56,144,24),x,y,ev)){action(5);return;}
    if(ui_click(ui_r(162,ch-56,112,24),x,y,ev)){action(6);return;}
    if(ev==EV_PRESS&&x>=14&&x<cw-14&&y>=84&&y<ch-92){int n=scroll+(y-84)/18;if(n<count)selected=n;}
}
static void wheel(int i,int dz){(void)i;if(working)return;selected+=dz*3;if(selected<0)selected=0;if(selected>=count)selected=count?count-1:0;}
static int open_file(const char *name,const char *full,const u8 *bytes,int n)
{
    (void)bytes;(void)n;if(working||pending){api->notify("Archive Manager is busy. Wait for it to finish.");return 0;}
    char path[202];const char *p=name;
    if(full){
        if(api->strlen(full)>=sizeof path-2){api->notify("Archive path is too long.");return -1;}
        if(full[0]&&full[1]==':')p=full;
        else {api->kfmt(path,sizeof path,"u:%s",full);p=path;}
    }
    if(api->win_open(type)<0)return -1;
    api->strlcpy(pending_path,p,sizeof pending_path);request_action(4);return 0;
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,0,"Archive Manager"};
int kext_entry(const Kapi *k)
{
    api=k;ui_init(k,0);static const AppDesc d={.live_draw=APP_INDEPENDENT|APP_CLOSE_REQUEST,.title="Archive Manager",.max_inst=1,.in_menu=1,.resizable=1,.category=APP_CAT_PROGRAMS,
        .open=opened,.close=closed,.draw=draw,.key=key,.mouse=mouse,.wheel=wheel,.drop=dropped,.client_size=initial,.min_client=size};
    type=k->register_app(&d);if(type<0)return 1;
    k->register_opener("fpa",open_file);k->register_opener("pz",open_file);return 0;
}
