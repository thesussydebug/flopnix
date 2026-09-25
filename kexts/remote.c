#include "kapi.h"
#include "netlisten.h"
#include "shellstream.h"
#include "kextfile.h"
#include "telnet.inc"

static const Kapi *api;
static const NetListenOps *net;
static const ShellStreamOps *shell;
static const KextFileOps *files;
static ShellStream stream;
static Telnet telnet;
static char line[1100],target[256];
static u32 session,activity,upload_size,upload_crc,upload_got;
static u8 *upload;
static u8 output[128];
static int enabled,machine,used,overflow,output_n,io_failed,servicing,stop_requested;
static u16 port=23;
static int upload_managed;
static void managed_commit(void);

#define ticks (*api->ticks)

static void emit(char c,void *ctx);

static u32 remote_crc(const u8 *data,u32 length)
{
    u32 crc=~0u;
    for(u32 i=0;i<length;i++){
        crc^=data[i];
        for(int j=0;j<8;j++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
static void raw(const u8 *data,int length)
{
    u32 stamp=ticks;
    while(length&&!io_failed){
        int n=net->write(session,data,length);
        if(n<0||(u32)(ticks-stamp)>1000u){io_failed=1;break;}
        data+=n;length-=n;if(n)stamp=ticks;
        if(length){net->poll();api->gui_pump();}
    }
}
static void text(const char *s){raw((const u8 *)s,(int)api->strlen(s));}
static void upload_clear(void)
{
    api->kfree(upload);upload=0;upload_size=upload_got=0;target[0]=0;upload_managed=0;
}
static void reset(void)
{
    upload_clear();api->memset(line,0,sizeof line);api->memset(&telnet,0,sizeof telnet);
    api->memset(&stream,0,sizeof stream);stream.putc=emit;
    used=overflow=machine=output_n=io_failed=0;
}
static void stop(void)
{
    net->stop();reset();
    session=0;enabled=0;stop_requested=0;api->klog("remote: disabled\n");
}
static void hexline(const char *prefix,const u8 *data,int n)
{
    static const char digits[]="0123456789abcdef";
    char encoded[1032];int at=0;
    while(*prefix)encoded[at++]=*prefix++;
    for(int i=0;i<n;i++){encoded[at++]=digits[data[i]>>4];encoded[at++]=digits[data[i]&15];}
    encoded[at++]='\r';encoded[at++]='\n';raw((const u8 *)encoded,at);
}
static void flush(void)
{
    if(output_n){hexline("@out ",output,output_n);output_n=0;}
}
static void emit(char c,void *ctx)
{
    (void)ctx;
    if(machine){output[output_n++]=(u8)c;if(output_n==(int)sizeof output||c=='\n')flush();}
    else{
        u8 b[2]={(u8)c,0};int n=1;
        if(c=='\n'){b[0]='\r';b[1]='\n';n=2;}
        else if(c=='\r'){b[1]=0;n=2;}
        else if((u8)c==255){b[1]=255;n=2;}
        raw(b,n);
    }
}
static void done(int result)
{
    char b[40];flush();api->kfmt(b,sizeof b,"@done %d\r\n",result);text(b);
}
static void error(const char *s)
{
    text("@error ");text(s);text("\r\n");done(-1);
}
static void prompt(void)
{
    char b[128];api->kfmt(b,sizeof b,"root@flopnix:/%s# ",stream.cwd);text(b);
}
static int digit(int c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static int number(const char **input,u32 *value,int base)
{
    const char *s=*input;u32 n=0;int count=0;
    while(*s&&*s!=' '){
        int d=digit(*s++);if(d<0||d>=base||n>(~0u-(u32)d)/(u32)base)return 0;
        n=n*(u32)base+(u32)d;count++;
    }
    if(!count)return 0;
    while(*s==' ')s++;*input=s;*value=n;return 1;
}
static int path_ok(const char *path)
{
    int n=(int)api->strlen(path);
    if(n<8||n>=FS_NAMELEN||api->strncmp(path,"sys/",4)||api->strcmp(path+n-3,".kx"))return 0;
    for(int i=4;i<n-3;i++){
        char c=path[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
    }
    return 1;
}
static void put(const char *args)
{
    upload_clear();int n=0;
    while(*args&&*args!=' '){if(n>=FS_NAMELEN-1){error("Invalid kext path");return;}target[n++]=*args++;}
    target[n]=0;while(*args==' ')args++;
    if(!path_ok(target)||!number(&args,&upload_size,10)||!number(&args,&upload_crc,16)||*args||
       upload_size<52||upload_size>FS_MAXFILE){upload_clear();error("Usage: @put sys/name.kx bytes crc32");return;}
    upload=api->kmalloc(upload_size);
    if(!upload){upload_clear();error("Not enough memory");return;}
    text("@ready\r\n");done(0);
}
static void data(const char *s)
{
    int length=(int)api->strlen(s);
    if(!upload||!length||(length&1)||length>1024||(u32)(length/2)>upload_size-upload_got){
        upload_clear();error("Invalid upload chunk; transfer discarded");return;
    }
    for(int i=0;i<length;i+=2){
        int a=digit(s[i]),b=digit(s[i+1]);
        if(a<0||b<0){upload_clear();error("Invalid hex data; transfer discarded");return;}
        upload[upload_got++]=(u8)((a<<4)|b);
    }
    done(0);
}
static void commit(void)
{
    if(upload_managed){managed_commit();return;}
    KextHeader header;
    if(!upload||upload_got!=upload_size||remote_crc(upload,upload_size)!=upload_crc){
        upload_clear();error("Incomplete upload or checksum mismatch; file unchanged");return;
    }
    int r=files->check(upload,upload_size,&header);
    if(r){upload_clear();error(r==43?"Kext needs a newer kernel API; file unchanged":"Invalid kext image; file unchanged");return;}
    u8 *check=api->kmalloc(upload_size);
    if(!check){upload_clear();error("Not enough memory for readback; file unchanged");return;}
    r=files->replace(target,upload,upload_size);
    if(r){api->kfree(check);upload_clear();error(r==-2?"Not enough free disk space for replacement":"Disk write failed; check the floppy before retrying");return;}
    r=api->fs_read(target,check,upload_size);
    int valid=r==(int)upload_size&&remote_crc(check,upload_size)==upload_crc;
    api->kfree(check);
    if(valid){text("@saved ");text(target);text(" - verified; reboot to activate\r\n");api->klog("remote: kext file replaced; reboot pending\n");}
    upload_clear();
    if(valid)done(0);else error("Readback failed; verify the floppy before reboot");
}
static void get(const char *path)
{
    if(!path_ok(path)){error("Invalid kext path");return;}
    u32 size=0;
    if(api->fs_exists(path))for(int i=0;i<FS_NFILES;i++){
        FsEnt *e=api->fs_slot(i);if(e&&e->used&&!api->strcmp(e->name,path)){size=e->size;break;}
    }
    if(!size||size>FS_MAXFILE){error("Kext file not found");return;}
    u8 *buf=api->kmalloc(size);
    if(!buf){error("Not enough memory");return;}
    int n=api->fs_read(path,buf,size);
    if(n!=(int)size){api->kfree(buf);error("Cannot read kext");return;}
    char b[64];api->kfmt(b,sizeof b,"@file %u %08x\r\n",size,remote_crc(buf,size));text(b);
    for(u32 i=0;i<size&&!io_failed;i+=512){u32 take=size-i;if(take>512)take=512;hexline("@data ",buf+i,(int)take);}
    api->kfree(buf);done(0);
}
#include "management.inc"

static void command(void)
{
    line[used]=0;
    if(overflow){if(machine)error("Line too long; command discarded");else{text("\r\nLine too long; command discarded.\r\n");prompt();}}
    else if(!api->strcmp(line,"@manager 1")){machine=1;text("\r\nFXR1 READY\r\n");}
    else if(!api->strcmp(line,"exit")||!api->strcmp(line,"logout")){text("\r\nGoodbye.\r\n");net->close(session);}
    else if(machine){
        if(!api->strncmp(line,"@exec ",6)){
            if(upload)error("Upload active; commit or abort first");
            else{int r=shell->run(&stream,line+6);if(r==-2)text("@error Shell is busy; retry\r\n");done(r);}
        }else if(!api->strncmp(line,"@manage ",8))managed_command(line+8);
        else if(!api->strncmp(line,"@put ",5))put(line+5);
        else if(!api->strncmp(line,"@data ",6))data(line+6);
        else if(!api->strcmp(line,"@commit"))commit();
        else if(!api->strcmp(line,"@abort")){upload_clear();done(0);}
        else if(!api->strncmp(line,"@get ",5))get(line+5);
        else error("Unknown manager request");
    }else{
        text("\r\n");
        if(used){int r=shell->run(&stream,line);if(r)text(r==-2?"Shell is busy; retry.\r\n":"Command failed.\r\n");}
        prompt();
    }
    api->memset(line,0,sizeof line);used=overflow=0;
}
static void poll(void *unused)
{
    (void)unused;if(!enabled||servicing)return;servicing=1;
    u32 current=net->session();
    if(current!=session){
        reset();session=current;activity=ticks;
        if(session){
            static const u8 hello[]={255,251,1,255,251,3,255,253,3};
            raw(hello,sizeof hello);text("FLOPNIX Remote 1\r\nType help, or exit to disconnect.\r\n");prompt();
            api->klog("remote: connected\n");
        }
    }
    if(session){
        if((u32)(ticks-activity)>60000u){
            text("\r\nSession timed out.\r\n");net->close(session);
        }else{
            u8 input[512];int n=net->read(session,input,sizeof input);
            if(n==-2)net->close(session);
            for(int i=0;i<n&&!io_failed;i++){
                int c=telnet_byte(&telnet,input[i],raw);if(c<0)continue;activity=ticks;
                if(c==10){command();if(net->session()!=session)break;}
                else if(c==3){used=overflow=0;if(!machine){text("^C\r\n");prompt();}}
                else if(c==8||c==127){if(used&&!overflow){used--;if(!machine)text("\b \b");}}
                else if(c>=32&&c<127){
                    int limit=machine?(int)sizeof line-1:191;
                    if(used>=limit)overflow=1;
                    else if(!overflow){line[used++]=(char)c;if(!machine){u8 b=(u8)c;raw(&b,1);}}
                }
            }
        }
        if(io_failed)net->close(session);
    }
    if(stop_requested){
        net->close(session);net->poll();u32 start=ticks;
        while(net->pending(session)&&(u32)(ticks-start)<200u){net->poll();api->gui_pump();}
        stop();
    }
    servicing=0;
}
static void control(const char *args)
{
    if(!*args||!api->strcmp(args,"status")){
        char b[128];u32 ip=api->net_get(NET_IP),automatic=0;api->config_get("remote.auto",&automatic);
        api->kfmt(b,sizeof b,"Remote: %s; %u.%u.%u.%u:%u; %s\n",enabled?"enabled":"off",
            ip&255,(ip>>8)&255,(ip>>16)&255,ip>>24,(u32)port,!enabled?"not listening":session?"connected":ip?"ready":"waiting for network address");
        api->shell_print(b);
        api->shell_print(automatic==1?"Automatic Remote: on at boot (Debug menu, Ctrl+D).\n":"Automatic Remote: off (Debug menu, Ctrl+D).\n");return;
    }
    if(!api->strcmp(args,"off")){
        if(servicing)stop_requested=1;else stop();
        api->shell_print("Remote access disabled for this session.\n");return;
    }
    if(api->strcmp(args,"on")&&api->strncmp(args,"on ",3)){
        api->shell_print("Usage: remote on [port] | off | status\n");return;
    }
    args+=2;while(*args==' ')args++;
    u32 selected=23;
    if((*args&&(!number(&args,&selected,10)||*args))||!selected||selected>65535){
        api->shell_print("Use remote on, or remote on <port from 1 to 65535>.\n");return;
    }
    if(enabled&&port==(u16)selected){control("status");return;}
    if(servicing){api->shell_print("Change the remote port from the local terminal.\n");return;}
    if(net->listen((u16)selected)){
        api->shell_print("Remote: networking is unavailable or the port is in use.\n");return;
    }
    reset();session=0;port=(u16)selected;enabled=1;
    api->klog("remote: enabled locally\n");control("status");
    api->shell_print("No password: enable only on a trusted network.\nUse remote off to stop; Debug > Automatic Remote controls startup.\n");
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_KERNEL,0,"Remote access"};
int kext_entry(const Kapi *k)
{
    api=k;if(k->version<KAPI_VERSION)return 1;
    net=api->service_get("net.listen");shell=api->service_get("shell.stream");files=api->service_get("kext.files");
    if(!net||net->abi!=NET_LISTEN_ABI||!shell||shell->abi!=SHELL_STREAM_ABI||!files||files->abi!=KEXT_FILE_ABI)return 1;
    if(api->timer_add(1,poll,0)<0)return 1;
    api->register_shutdown(stop);
    int result=api->register_cmd("remote","remote [on [port]|off|status] - remote terminal access\n  on: enable now; off: stop this session; status: show address and startup setting\n  Ctrl+D > Automatic Remote: save whether remote starts at boot",control);
    u32 automatic=0;
    if(!result&&api->config_get("remote.auto",&automatic)&&automatic==1)control("on");
    return result;
}
