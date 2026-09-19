#include "kapi.h"
#include "gdi.h"
#include "nethttp.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "textweb_core.inc"
#pragma clang diagnostic pop
#include "update_core.inc"
#include "dynbuf.h"
#include "ui.inc"
static const Kapi *api;
#include "appfield.h"
typedef struct {u32 magic;char address[160],code[33];u8 pad[3];u32 floor;u8 digest[32];u32 crc;} UpdatePrefs;
static UpdatePrefs prefs;
static char address[160],code[65],status[100];
static AppField fields[2];
static int focus,busy,closing,ready,installed;
static KuRelease release;
typedef struct {DynBuf buf;u32 len,limit,started;int failed;} Download;
static char report[40][96];
static int report_count,report_top;
static void log_line(const char *s)
{
    u32 n=api->strlen(s);if(n>59){u32 cut=59;while(cut&&s[cut]!=' ')cut--;if(!cut)cut=59;
        char part[60];api->memcpy(part,s,cut);part[cut]=0;log_line(part);log_line(s+cut+(s[cut]==' '));return;}
    if(report_count==40){api->memmove(report,report+1,sizeof report-sizeof report[0]);report_count--;}
    api->strlcpy(report[report_count++],s,sizeof report[0]);report_top=report_count>8?report_count-8:0;
    api->gui_dirty();
}
static void say(const char *s){api->strlcpy(status,s,sizeof status);log_line(s);}
static void log_ip(const char *label,u32 ip)
{
    char b[96];api->kfmt(b,sizeof b,"%s: %u.%u.%u.%u",label,ip&255,(ip>>8)&255,(ip>>16)&255,ip>>24);log_line(b);
}
static void snapshot(void)
{
    char b[96];u32 card=api->net_get(NET_ADAPTER),link=api->net_get(NET_LINK),state=api->net_get(NET_STATE);
    static const char *const cards[]={"none","NE2000 PCI","RTL8139","DEC/ADMtek Tulip","AMD PCnet","NE2000 ISA"};
    static const char *const states[]={"manual / waiting","discovering","requesting","bound","renewing","rebinding"};
    api->kfmt(b,sizeof b,"Adapter: %s; I/O %04x",card<6?cards[card]:"unknown",api->net_get(NET_IO));log_line(b);
    const u8 *mac=api->net_mac();if(mac){api->kfmt(b,sizeof b,"MAC: %02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);log_line(b);}
    log_line(!(link&NET_LINK_PHY)?"Link: card cannot report cable status":link&NET_LINK_UP?"Link: UP":"Link: DOWN - check cable and switch port");
    api->kfmt(b,sizeof b,"Address state: %s; lease %u seconds",state<6?states[state]:"unknown",api->net_get(NET_LEASE_LEFT));log_line(b);
    log_ip("IP",api->net_get(NET_IP));log_ip("Mask",api->net_get(NET_MASK));log_ip("Router",api->net_get(NET_GW));log_ip("DNS",api->net_get(NET_DNS));log_ip("Backup DNS",api->net_get(NET_DNS2));
    api->kfmt(b,sizeof b,"Packets received %u; sent %u",api->net_get(NET_RX),api->net_get(NET_TX));log_line(b);
}
static void save_report(void)
{
    char data[40*97];u32 n=0;
    for(int i=0;i<report_count;i++){u32 len=api->strlen(report[i]);api->memcpy(data+n,report[i],len);n+=len;data[n++]='\n';}
    if(api->fs_write("update-log.txt",(const u8 *)data,n))say("Could not save report. Check floppy space/write protection.");
    else say("Saved A:/update-log.txt (pairing key excluded).");
}
static int save_prefs(void)
{
    prefs.magic=0x31505546;prefs.crc=api->crc32(&prefs,sizeof prefs-4);
    if(api->fs_write("sys/update.cfg",(const u8 *)&prefs,sizeof prefs)){say("Could not save update settings to A:.");return 0;}return 1;
}
static int settings(void)
{
    TwUrl u;u8 key[16];char canonical[33];
    if(!tw_url(address,&u)||!u.http||api->strcmp(u.path,"/")){say("Use http://host:port/ without a path.");return 0;}
    if(!ku_pair_key(code,key)){say("Enter the grouped pairing code or original 32-digit key.");return 0;}
    ku_hex(key,16,canonical);
    if(api->strcmp(prefs.code,canonical)){prefs.floor=0;api->memset(prefs.digest,0,32);}
    api->strlcpy(prefs.address,address,sizeof prefs.address);api->strlcpy(prefs.code,canonical,sizeof prefs.code);
    return save_prefs();
}
static int receive(const u8 *data,int n,void *ctx)
{
    Download *d=ctx;
    if(closing||api->esc_pending()){d->failed=1;return 0;}
    if((u32)(*api->ticks-d->started)>6000){d->failed=3;return 0;}
    if(n<0||(u32)n>d->limit-d->len){d->failed=4;return 0;}
    if(!db_reserve(api,&d->buf,d->len+(u32)n,1,1024,d->limit,"Kernel download")){d->failed=2;return 0;}
    api->memcpy((u8 *)d->buf.data+d->len,data,(u32)n);d->len+=(u32)n;return 1;
}
static int fetch(const char *path,Download *d)
{
    TwUrl u;char b[96];const NetHttpOps *http=api->service_get("net.http");
    snapshot();
    if(!http){say("HTTP service missing. Copy matching sys/net.kx and reboot.");return 0;}
    if(http->abi!=NET_HTTP_ABI){say("HTTP service incompatible. Replace net.kx and reboot.");return 0;}
    log_line("HTTP service: ready");
    if(!api->net_up()){say("No supported adapter detected. Run lspci and netdiag.");return 0;}
    if(!api->net_get(NET_IP)){say("No IPv4 address. Settings > Network: DHCP or manual setup.");return 0;}
    if(!tw_url(address,&u)||!u.http||api->strcmp(u.path,"/")){say("Use http://host:port/ without a path.");return 0;}
    api->kfmt(b,sizeof b,"Host: %s",u.host);log_line(b);
    if(!api->strcmp(u.host,"10.0.2.2"))log_line("10.0.2.2 is for QEMU. Bare metal needs the PC's LAN IP.");
    d->started=*api->ticks;api->esc_arm();u32 ip=api->net_dns(u.host,400);
    if(!ip){say("Name lookup failed. Try the Windows host's numeric LAN IP.");return 0;}
    log_ip("Resolved host",ip);
    if(closing||api->esc_pending()){say("Update cancelled.");return 0;}
    u32 own=api->net_get(NET_IP),mask=api->net_get(NET_MASK),gw=api->net_get(NET_GW);
    if((ip&mask)!=(own&mask)&&!gw){say("Host is outside the subnet and no router is configured.");return 0;}
    log_ip("Next hop",(ip&mask)==(own&mask)?ip:gw);
    api->kfmt(b,sizeof b,"Connecting to TCP port %u; %s",u.port,d->limit==KU_MANIFEST?"manifest":"kernel");log_line(b);
    char host[90];api->kfmt(host,sizeof host,"%s:%u",u.host,u.port);
    NetHttpInfo info;api->memset(&info,0,sizeof info);
    u32 rx=api->net_get(NET_RX),tx=api->net_get(NET_TX);
    int result=http->get(ip,u.port,host,path,receive,d,1000,&info);
    api->kfmt(b,sizeof b,"HTTP result %d; status %d; received %u bytes",result,info.status,d->len);log_line(b);
    api->kfmt(b,sizeof b,"Content length: %s %u; elapsed %u ticks",info.length_known?"known":"unknown",info.length,(u32)(*api->ticks-d->started));log_line(b);
    api->kfmt(b,sizeof b,"Packet change: received %u; sent %u",api->net_get(NET_RX)-rx,api->net_get(NET_TX)-tx);log_line(b);
    const NetHttpDiagOps *diag=api->service_get("net.http.diag");NetHttpDiag trace={0};
    if(diag&&diag->abi==NET_HTTP_DIAG_ABI){diag->read(&trace);if(trace.result!=result)trace.stage=NH_IDLE;}
    if(result!=200||d->failed||closing||api->esc_pending()||(info.length_known&&info.length!=d->len)){
        if(closing||api->esc_pending()||d->failed==1)say("Update cancelled. Kernel unchanged.");
        else if(d->failed==2)say("Not enough memory. Close apps and retry; kernel unchanged.");
        else if(d->failed==3)say("Download exceeded the 60-second limit. Kernel unchanged.");
        else if(d->failed==4)say("Server sent more data than allowed. Kernel unchanged.");
        else if(result==-5)say("Network transfer busy. Close browser/downloads and retry.");
        else if(trace.stage==NH_ARP)say("ARP failed: no next-hop reply. Check cable, IP and subnet.");
        else if(trace.stage==NH_CONNECT)say("TCP connect failed. Check host LAN mode, port and firewall.");
        else if(trace.stage==NH_RESPONSE)say("Connected, but HTTP reply failed or was cut short. See host log.");
        else if(trace.stage==NH_PREFLIGHT&&result==-4)say("Not enough memory for HTTP. Close apps and retry.");
        else if(result>=100){api->kfmt(b,sizeof b,"HTTP %d or incomplete body. Check host request log.",result);say(b);}
        else say("HTTP transfer failed. Check host log, connection and memory.");
        return 0;
    }
    log_line("HTTP download complete.");return 1;
}
static void diagnose(void)
{
    if(busy)return;busy=1;closing=0;report_count=report_top=0;
    say("Testing host access without a pairing key. Esc cancels.");
    Download d={0};d.limit=KU_MANIFEST;
    if(fetch("/manifest.bin",&d)){
        if(d.len==KU_MANIFEST&&ku_equal(d.buf.data,(const u8 *)"FXU1",4))say("Host reachable. Use Check to authenticate before installing.");
        else say("HTTP works, but this is not a FLOPNIX update manifest.");
    }
    db_free(api,&d.buf);busy=0;api->gui_dirty();
}
static int current_matches(void)
{
    u8 sector[512],hash[32];KuSha sha;ku_sha_init(&sha);
    for(u32 i=0;i<release.size/512;i++){
        if(closing||api->esc_pending()||api->disk_read(i+1,sector))return -1;
        if(!i&&(u32)(sector[6]|sector[7]<<8)!=release.size/512)return 0;
        ku_sha_feed(&sha,sector,512);
    }
    ku_sha_done(&sha,hash);return ku_equal(hash,release.digest,32);
}
static void check(void)
{
    if(busy)return;ready=installed=0;report_count=report_top=0;if(!settings())return;
    busy=1;closing=0;say("Checking the host... Esc cancels.");
    Download d={0};d.limit=KU_MANIFEST;
    if(fetch("/manifest.bin",&d)){
        u8 key[16];ku_pair_key(code,key);
        if(!ku_manifest(d.buf.data,d.len,key,prefs.floor,prefs.digest,api->version,&release)){
            if(d.len!=KU_MANIFEST||!ku_equal(d.buf.data,(const u8 *)"FXU1",4))say("Invalid manifest format or size. Check the selected host.");
            else {u8 mac[32];ku_hmac(key,16,d.buf.data,48,mac);
                if(!ku_equal(mac,(u8 *)d.buf.data+48,32))say("Pairing code mismatch or altered manifest. Recheck the code.");
                else say("Authenticated manifest rejected: older or incompatible release.");}
        }
        else {int match=current_matches();installed=match==1;ready=match==0&&!closing&&!api->esc_pending();
            if(match<0)say("Could not read the installed kernel, or check cancelled.");
            else if(installed)say("This kernel is already installed.");
            else if(ready)api->kfmt(status,sizeof status,"Authenticated release %u (%u KiB). Ready to install.",release.sequence,release.size/1024);
            else say("Update cancelled.");}
    }
    db_free(api,&d.buf);busy=0;api->gui_dirty();
}
static void install_reply(int result,void *ctx)
{
    (void)ctx;if(result!=MBR_YES||busy||closing)return;
    if(!ready)check();if(!ready||closing)return;
    busy=1;say("Downloading the kernel... Esc cancels.");
    char path[72],hex[65];ku_hex(release.digest,32,hex);api->kfmt(path,sizeof path,"/%s.ku",hex);
    Download d={0};d.limit=release.size;
    if(!db_reserve(api,&d.buf,release.size,1,release.size,KU_MAX,"Kernel download")){say("Not enough memory for this kernel. Close apps and retry.");busy=0;return;}
    if(fetch(path,&d)){
        if(!ku_image(d.buf.data,d.len,&release))say("Kernel verification failed. Nothing was installed.");
        else {
            prefs.floor=release.sequence;api->memcpy(prefs.digest,release.digest,32);
            if(save_prefs()&&!closing&&!api->esc_pending()){
                char err[100];say("Installing. Keep the floppy inserted and power on.");
                api->buffer_lock();int r=api->kernel_update_data(d.buf.data,d.len,err,sizeof err);api->buffer_unlock();
                if(r)say(err);
            }
        }
    }
    db_free(api,&d.buf);busy=0;api->gui_dirty();
}
static void install(void)
{
    if(!busy)api->msgbox("Check and install kernel?","Check the paired host and install a new kernel if available? This writes the boot floppy and restarts FLOPNIX. Keep power on and the disk inserted. Extensions are not replaced.",MB_YESNO,install_reply,0);
}
static void opened(int inst)
{
    (void)inst;closing=ready=installed=0;report_count=report_top=0;api->memset(&prefs,0,sizeof prefs);
    int n=api->fs_read("sys/update.cfg",(u8 *)&prefs,sizeof prefs);
    if(n!=sizeof prefs||prefs.magic!=0x31505546||prefs.crc!=api->crc32(&prefs,sizeof prefs-4)||prefs.address[159]||prefs.code[32]){
        api->memset(&prefs,0,sizeof prefs);api->strlcpy(prefs.address,"http://10.0.2.2:8080/",sizeof prefs.address);
    }
    char display[33];u8 key[16];
    if(ku_pair_key(prefs.code,key))ku_pair_text(key,display);else display[0]=0;
    af_set(&fields[0],address,sizeof address,prefs.address);af_set(&fields[1],code,sizeof code,display);
    focus=0;say("Enter the address and pairing code shown by Update Host.");
}
static void closed(int inst){(void)inst;closing=1;ready=0;}
static void size(int inst,int *w,int *h){(void)inst;*w=520;*h=424;}
static void draw(Win *w,int x,int y,int cw,int ch)
{
    (void)w;(void)ch;ui_header(x,y,cw,"Kernel Update");
    api->draw_text(x+12,y+38,"Host address",C_BLACK);af_draw(&fields[0],x+12,y+58,cw-24,focus==0);
    api->draw_text(x+12,y+94,"Pairing code",C_BLACK);af_draw(&fields[1],x+12,y+114,cw-24,focus==1);
    ui_button(x,y,ui_r(12,154,92,26),"Save",0,!busy);
    ui_button(x,y,ui_r(114,154,116,26),"Check",0,!busy);
    ui_button(x,y,ui_r(240,154,148,26),ready?"Install + restart":"Check + update",0,!busy);
    char first[64];u32 cut=api->strlen(status);if(cut>60){cut=60;while(cut&&status[cut]!=' ')cut--;if(!cut)cut=60;}
    api->memcpy(first,status,cut);first[cut]=0;api->draw_text_clip(x+12,y+198,first,C_BLACK,cw-24);
    if(status[cut])api->draw_text_clip(x+12,y+214,status+cut+(status[cut]==' '),C_BLACK,cw-24);
    ui_button(x,y,ui_r(12,240,104,24),"Diagnose",0,!busy);
    ui_button(x,y,ui_r(124,240,104,24),"Save log",0,!busy);
    ui_button(x,y,ui_r(340,240,76,24),"Earlier",0,!busy&&report_top>0);
    ui_button(x,y,ui_r(424,240,76,24),"Later",0,!busy&&report_top+8<report_count);
    api->panel(x+12,y+272,cw-24,140,1);
    for(int i=0;i<8&&report_top+i<report_count;i++)api->draw_text_clip(x+18,y+278+i*16,report[report_top+i],C_BLACK,cw-36);
}
static void key(int inst,int key)
{
    (void)inst;if(busy)return;if(key=='\t'){focus=!focus;return;}if(key=='\n'){check();return;}
    if(key==K_PGUP){report_top=report_top>8?report_top-8:0;return;}
    if(key==K_PGDN){report_top+=8;if(report_top>report_count-8)report_top=report_count>8?report_count-8:0;return;}
    af_key(&fields[focus],key);ready=0;
}
static void mouse(int inst,int x,int y,int ev,int cw,int ch)
{
    (void)inst;(void)ch;if(busy)return;
    for(int n=0;n<2;n++)if(af_mouse(&fields[n],12,58+n*56,cw-24,x,y,ev)){focus=n;return;}
    if(ev!=EV_PRESS)return;
    if(ui_hit(ui_r(12,58,cw-24,24),x,y))focus=0;
    else if(ui_hit(ui_r(12,114,cw-24,24),x,y))focus=1;
    else if(ui_hit(ui_r(12,154,92,26),x,y)){ready=0;if(settings())say("Host settings saved. Select Check.");}
    else if(ui_hit(ui_r(114,154,116,26),x,y))check();
    else if(ui_hit(ui_r(240,154,148,26),x,y))install();
    else if(ui_hit(ui_r(12,240,104,24),x,y))diagnose();
    else if(ui_hit(ui_r(124,240,104,24),x,y))save_report();
    else if(ui_hit(ui_r(340,240,76,24),x,y))key(inst,K_PGUP);
    else if(ui_hit(ui_r(424,240,76,24),x,y))key(inst,K_PGDN);
}
const KextHeader kext_header={KEXT_MAGIC,KAPI_VERSION,KEXT_KIND_APP,KEXT_RECLAIMABLE,"Kernel Update"};
int kext_entry(const Kapi *k)
{
    if(k->version<KAPI_VERSION)return 1;api=k;ui_init(k,0);
    static const AppDesc d={.title="Kernel Update",.max_inst=1,.in_menu=1,.category=APP_CAT_SYSTEM,
        .open=opened,.close=closed,.draw=draw,.key=key,.mouse=mouse,.client_size=size,.live_draw=APP_INDEPENDENT};
    return api->register_app(&d)<0;
}
