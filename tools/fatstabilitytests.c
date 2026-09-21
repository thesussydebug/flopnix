/* Tests the FAT driver with simulated sectors and device failures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kapi.h"
static void *copy_bytes(void *d,const void *s,u32 n){return memcpy(d,s,n);}
static void *fill_bytes(void *d,int c,u32 n){return memset(d,c,n);}
static u32 length(const char *s){return (u32)strlen(s);}
static void copy_string(char *d,const char *s,int n){if(n)snprintf(d,n,"%s",s);}
#include "../kexts/fat.c"
#undef memcpy
#undef memset
#undef memmove
#undef strlen
#undef strcmp
#undef strncmp
#undef strcasecmp
#undef strlcpy
#undef rtc_now_dos
#undef usb_present
#undef usb_gen
#undef usb_read
#undef usb_write
#undef usb_capacity_sectors
#define SECTORS 131072
static u8 disk[SECTORS*512], payload[1024], result[1024];
static u32 generation=1, fail_read=~0u, fail_write=~0u;
static int checks, failures, outside, writes, commit_error;
static u32 largest_read, largest_write;
static u32 volume_sectors=8192;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static int present(void){return 1;}
static u32 gen(void){return generation;}
static u32 capacity(void){return volume_sectors;}
static u32 date(void){return 0;}
static int read_disk(u32 l,u32 n,u8 *b){if(n>largest_read)largest_read=n;if(l>=volume_sectors||n>volume_sectors-l){outside++;return -1;}if(l<=fail_read&&fail_read-l<n)return -1;memcpy(b,disk+l*512,n*512);return 0;}
static int write_disk(u32 l,u32 n,const u8 *b){if(n>largest_write)largest_write=n;if(l>=volume_sectors||n>volume_sectors-l){outside++;return -1;}int bad=l<=fail_write&&fail_write-l<n;if(bad&&!commit_error)return -1;writes++;memcpy(disk+l*512,b,n*512);return bad?-1:0;}
static Kapi mock={.strlen=length,.strlcpy=copy_string,.strcmp=strcmp,.strcasecmp=_stricmp,
    .memcpy=copy_bytes,.memset=fill_bytes,.rtc_now_dos=date,.usb_present=present,
    .usb_gen=gen,.usb_read=read_disk,.usb_write=write_disk,.usb_capacity_sectors=capacity};
static void put16(u8 *p,u32 v){p[0]=v;p[1]=v>>8;}
static void put32(u8 *p,u32 v){put16(p,v);put16(p+2,v>>16);}
static void reset(void){
    memset(disk,0,sizeof disk);generation++;fail_read=fail_write=~0u;outside=writes=commit_error=0;largest_read=largest_write=0;volume_sectors=8192;
    disk[0]=0xeb;put16(disk+11,512);disk[13]=1;put16(disk+14,1);disk[16]=2;
    put16(disk+17,128);put16(disk+19,volume_sectors);put16(disk+22,32);disk[510]=0x55;disk[511]=0xaa;
    for(int f=0;f<2;f++){put16(disk+(1+f*32)*512,0xfff8);put16(disk+(1+f*32)*512+2,0xffff);}
    api=&mock;for(int i=0;i<1024;i++)payload[i]=(u8)(i*37+1);
}
static u8 *entry(void){return disk+65*512;}
static void invalidate(void){cache_lba=~0u;}
static void set_link(u32 c,u32 v){for(int f=0;f<2;f++)put16(disk+(1+f*32)*512+c*2,v);invalidate();}
static void long_name(const char *name){
    u8 *d=entry();memmove(d+32,d,32);memset(d,0xff,32);d[0]=0x41;d[11]=15;d[12]=0;d[13]=lfn_checksum(d+32);put16(d+26,0);
    const int offsets[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    for(int i=0;i<13;i++){put16(d+offsets[i],*name?(u8)*name++:0);}
    invalidate();
}
int main(int argc,char **argv){
    setvbuf(stdout,0,_IONBF,0);
    if(argc>1){reset();FILE *f=fopen(argv[1],"rb");if(!f)return 2;volume_sectors=(u32)(fread(disk,1,sizeof disk,f)/512);fclose(f);int ok=fat_mount();printf("Mount %d: capacity %u fat %u data %u clusters %u type %d writable %d\n",ok,volume_sectors,fat_lba,data_lba,total_clus,fattype,fat_writable());return !ok;}
    reset();CHECK(fat_mount()&&fattype==16);CHECK(fat_write("/file.txt",payload,512)==0);
    u32 c=rd16(entry()+26);fail_write=65;CHECK(fat_delete("/file.txt")<0);
    CHECK(rd16(disk+512+c*2)!=0);fail_write=~0u;invalidate();CHECK(fat_read("/file.txt",result,512)==512&&!memcmp(payload,result,512));
    reset();CHECK(fat_mkdir("/folder")==0);c=rd16(entry()+26);fail_write=65;CHECK(fat_rmdir("/folder")<0);CHECK(rd16(disk+512+c*2)!=0);
    reset();CHECK(fat_write("/file.txt",payload,512)==0);put32(entry()+28,1024);invalidate();CHECK(fat_read("/file.txt",result,1024)<0);
    reset();CHECK(fat_write("/file.txt",payload,1024)==0);c=rd16(entry()+26);set_link(c,0xfff7);CHECK(fat_read("/file.txt",result,1024)<0);CHECK(!outside);
    reset();CHECK(fat_write("/file.txt",payload,1024)==0);fail_read=1;invalidate();CHECK(fat_read("/file.txt",result,1024)<0);
    reset();put16(disk+19,8193);CHECK(!fat_mount());
    reset();put16(disk+22,1);CHECK(!fat_mount());
    reset();disk[13]=3;CHECK(!fat_mount());
    reset();CHECK(fat_write("/file.txt",payload,512)==0);put32(entry()+28,0xfffffff0u);invalidate();int before=writes;
    CHECK(fat_append("/file.txt",payload,64)<0);CHECK(writes==before);
    reset();CHECK(fat_write("/file.txt",payload,511)==0);CHECK(fat_append("/file.txt",payload+511,513)==0);
    CHECK(fat_read("/file.txt",result,1024)==1024&&!memcmp(payload,result,1024));
    CHECK(fat_delete("/file.txt")==0&&!fat_exists("/file.txt"));
    reset();CHECK(fat_write("/long~1.txt",payload,511)==0);long_name("Long File.txt");
    CHECK(fat_exists("/Long File.txt")==1);CHECK(fat_append("/Long File.txt",payload+511,513)==0);
    CHECK(fat_read("/Long File.txt",result,1024)==1024&&!memcmp(payload,result,1024));
    CHECK(fat_rename("/Long File.txt","new.txt")==0&&fat_exists("/new.txt"));
    reset();CHECK(fat_mkdir("/folder")==0);c=rd16(entry()+26);memset(disk+(73+c-2)*512,0xe5,512);set_link(c,c);
    FatEnt listing[4];CHECK(fat_list("/folder",listing,4)<0);CHECK(fat_rmdir("/folder")<0&&fat_exists("/folder")==2);
    set_link(c,0xffff);fail_read=1;invalidate();CHECK(fat_list("/folder",listing,4)<0);CHECK(fat_rmdir("/folder")<0);
    reset();CHECK(fat_write("/file.txt",payload,1024)==0);c=rd16(entry()+26);set_link(c,c);
    CHECK(fat_read("/file.txt",result,1024)<0);int prior=writes;CHECK(fat_append("/file.txt",payload,1)<0&&writes==prior);
    reset();CHECK(fat_write("/file.txt",payload,512)==0);c=rd16(entry()+26);fail_write=65;commit_error=1;
    CHECK(fat_write("/file.txt",payload+512,512)<0);u32 newer=rd16(entry()+26);
    CHECK(newer!=c&&rd16(disk+512+newer*2)!=0&&rd16(disk+512+c*2)!=0);
    fail_write=~0u;invalidate();CHECK(fat_read("/file.txt",result,512)==512&&!memcmp(payload+512,result,512));
    reset();fail_write=65;commit_error=1;CHECK(fat_mkdir("/folder")<0);c=rd16(entry()+26);CHECK(c>=2&&rd16(disk+512+c*2)!=0);
    reset();fail_read=65;CHECK(fat_list("/",listing,4)<0);CHECK(fat_write("/new.txt",payload,1)<0&&writes==0);
    reset();CHECK(fat_write("/file.txt",payload,512)==0);CHECK(fat_read("/file.txt",result,100)==100&&!memcmp(payload,result,100));

    reset();volume_sectors=2880;put16(disk+19,2880);put16(disk+22,9);put16(disk+17,224);
    CHECK(fat_mount()&&fattype==12&&!fat_writable());

    reset();volume_sectors=SECTORS;put16(disk+19,0);put32(disk+32,SECTORS);put16(disk+14,32);
    put16(disk+17,0);put16(disk+22,0);put32(disk+36,1024);put32(disk+44,2);put16(disk+40,0x81);
    put32(disk+(32+1024)*512+8,0x0fffffff);
    CHECK(fat_mount()&&fattype==32&&!fat_writable()&&fat_lba==1056);CHECK(fat_next(2)==0x0fffffff);
    CHECK(fat_list("/",listing,4)==0);

    reset();CHECK(fat_write("/long~1.txt",payload,1)==0);long_name("Long File.txt");
    struct { u8 guard[16]; LfnAcc acc; } guarded;
    memset(&guarded,0x7b,sizeof guarded);lfn_reset(&guarded.acc);lfn_feed(&guarded.acc,entry());
    char lname[64];CHECK(lfn_take(&guarded.acc,entry()+32,lname,sizeof lname)&&!strcmp(lname,"Long File.txt"));
    lfn_reset(&guarded.acc);lfn_feed(&guarded.acc,entry());u8 bad[32];memcpy(bad,entry(),32);bad[0]=0x80;lfn_feed(&guarded.acc,bad);
    int intact=1;for(int i=0;i<16;i++)if(guarded.guard[i]!=0x7b)intact=0;CHECK(intact);CHECK(!guarded.acc.ok);
    reset();CHECK(fat_write("/file.txt",payload,512)==0);put16(entry()+20,0x1234);invalidate();
    CHECK(fat_read("/file.txt",result,512)==512&&!memcmp(payload,result,512));
    reset();memmove(disk+64*512,disk,8192*512);memset(disk,0,64*512);volume_sectors=8256;
    disk[510]=0x55;disk[511]=0xaa;u8 *part=disk+0x1be + 16;part[4]=6;put32(part+8,64);put32(part+12,8192);
    CHECK(fat_mount()&&fat_lba==65);CHECK(fat_write("/file.txt",payload,512)==0);CHECK(fat_read("/file.txt",result,512)==512&&!memcmp(payload,result,512));
    reset();CHECK(fat_write("/long~1.txt",payload,512)==0);long_name("Long File.txt");
    int saved_writes=writes;CHECK(fat_rename("/Long File.txt","Long File.txt")==0&&writes==saved_writes);
    const char *bad_names[]={".","..","../gone.txt","dir/file.txt","bad\\name","bad:name","bad*name","bad?name","bad name ","name."};
    for(unsigned i=0;i<sizeof bad_names/sizeof *bad_names;i++){
        reset();CHECK(fat_write("/keep.txt",payload,512)==0);saved_writes=writes;
        CHECK(fat_rename("/keep.txt",bad_names[i])<0&&writes==saved_writes);
        CHECK(fat_read("/keep.txt",result,512)==512&&!memcmp(payload,result,512));
    }
    reset();CHECK(fat_write("/NET0001.PCAP",payload,512)==0);
    CHECK(entry()[11]==15&&entry()[0]==0x41&&entry()[13]==lfn_checksum(entry()+32));
    CHECK(fat_read("/NET0001.PCAP",result,512)==512&&!memcmp(payload,result,512));
    CHECK(fat_append("/NET0001.PCAP",payload+512,512)==0);
    CHECK(fat_read("/NET0001.PCAP",result,1024)==1024&&!memcmp(payload,result,1024));
    CHECK(fat_delete("/NET0001.PCAP")==0&&fat_exists("/NET0001.PCAP")==0);
    reset();CHECK(fat_write("/NET0001.PCA",payload,512)==0);prior=writes;
    CHECK(fat_write("/NET0001.PCAP",payload+512,512)<0&&writes==prior);
    CHECK(fat_read("/NET0001.PCA",result,512)==512&&!memcmp(payload,result,512));
    for(int entries=14;entries<=15;entries++){
        reset();char name[16];
        for(int i=0;i<entries;i++){snprintf(name,sizeof name,"/F%02d.TXT",i);CHECK(fat_write(name,payload,1)==0);}
        memset(entry()+(entries+2)*32,0x77,32);invalidate();
        CHECK(fat_write("/NET0001.PCAP",payload,512)==0);
        CHECK(fat_read("/NET0001.PCAP",result,512)==512&&!memcmp(payload,result,512));
        CHECK(entry()[(entries+2)*32]==0);
        CHECK(fat_write("/AFTER.TXT",payload,1)==0);
        CHECK(fat_read("/NET0001.PCAP",result,512)==512);
    }
    reset();fail_write=65;CHECK(fat_write("/NET0001.PCAP",payload,512)<0);
    fail_write=~0u;invalidate();CHECK(fat_exists("/NET0001.PCAP")==0);
    reset();fail_read=65;CHECK(fat_write("/NET0001.PCAP",payload,512)<0&&writes==0);
    reset();volume_sectors=32768;put16(disk+19,volume_sectors);disk[13]=4;
    CHECK(fat_write("/BATCH.BIN",payload,1024)==0);
    largest_read=0;CHECK(fat_read("/BATCH.BIN",result,1024)==1024&&!memcmp(payload,result,1024));
    CHECK(largest_read==2);
    memset(result,0xcc,sizeof result);CHECK(fat_read("/BATCH.BIN",result,513)==513&&!memcmp(payload,result,513));CHECK(result[513]==0xcc);
    fail_read=clus_lba(rd16(entry()+26))+1;invalidate();CHECK(fat_read("/BATCH.BIN",result,1024)<0);
    static u8 bulk[12289], got[12290];
    for(u32 i=0;i<sizeof bulk;i++)bulk[i]=(u8)(i*29+7);
    static const u32 prefixes[]={0,1,511,512,513,2047,2048,2049};
    for(u32 i=0;i<sizeof prefixes/sizeof prefixes[0];i++){
        reset();volume_sectors=32768;put16(disk+19,volume_sectors);disk[13]=4;
        u32 prefix=prefixes[i],extra=8193;
        CHECK(fat_write("/APPEND.BIN",bulk,prefix)==0);
        largest_write=0;CHECK(fat_append("/APPEND.BIN",bulk+prefix,extra)==0);
        CHECK(largest_write==4&&!outside);
        memset(got,0xcc,sizeof got);
        CHECK(fat_read("/APPEND.BIN",got,prefix+extra)==(int)(prefix+extra));
        CHECK(!memcmp(got,bulk,prefix+extra)&&got[prefix+extra]==0xcc);
    }
    reset();volume_sectors=32768;put16(disk+19,volume_sectors);disk[13]=4;
    CHECK(fat_write("/APPEND.BIN",bulk,513)==0);
    CHECK(fat_write("/KEEP.BIN",bulk+4096,4096)==0);
    largest_write=0;CHECK(fat_append("/APPEND.BIN",bulk+513,8193)==0);
    CHECK(largest_write==4&&!outside);
    CHECK(fat_read("/APPEND.BIN",got,8706)==8706&&!memcmp(got,bulk,8706));
    CHECK(fat_read("/KEEP.BIN",got,4096)==4096&&!memcmp(got,bulk+4096,4096));
    for(int commit=0;commit<2;commit++){
        reset();volume_sectors=32768;put16(disk+19,volume_sectors);disk[13]=4;
        CHECK(fat_write("/APPEND.BIN",bulk,513)==0);
        c=entry_cluster(entry());fail_write=clus_lba(c)+3;commit_error=commit;
        CHECK(fat_append("/APPEND.BIN",bulk+513,8193)<0);
        CHECK(cache_lba==~0u&&!outside);
        fail_write=~0u;CHECK(fat_read("/APPEND.BIN",got,sizeof got)==513&&!memcmp(got,bulk,513));
    }
    static u8 burst[524312],burst_read[524313];
    for(u32 i=0;i<sizeof burst;i++)burst[i]=(u8)(i*31+9);
    reset();volume_sectors=32768;put16(disk+19,volume_sectors);disk[13]=4;
    CHECK(fat_write("/NET0001.PCAP",burst,24)==0);
    largest_write=0;CHECK(fat_append("/NET0001.PCAP",burst+24,524288)==0);
    CHECK(largest_write==4&&!outside);memset(burst_read,0xcc,sizeof burst_read);
    CHECK(fat_read("/NET0001.PCAP",burst_read,sizeof burst_read)==sizeof burst);
    CHECK(!memcmp(burst_read,burst,sizeof burst)&&burst_read[sizeof burst]==0xcc);
    reset();volume_sectors=SECTORS;put16(disk+19,0);put32(disk+32,SECTORS);put16(disk+14,32);
    put16(disk+17,0);put16(disk+22,0);put32(disk+36,1024);put32(disk+44,2);
    for(int f=0;f<2;f++){put32(disk+(32+f*1024)*512,0x0ffffff8);put32(disk+(32+f*1024)*512+4,0x0fffffff);put32(disk+(32+f*1024)*512+8,0x0fffffff);}
    CHECK(fat_mount()&&fattype==32&&fat_writable());
    CHECK(fat_write("/NET0001.PCAP",payload,512)==0);
    CHECK(fat_read("/NET0001.PCAP",result,512)==512&&!memcmp(payload,result,512));
    CHECK(fat_append("/NET0001.PCAP",payload+512,512)==0);
    CHECK(fat_read("/NET0001.PCAP",result,1024)==1024&&!memcmp(payload,result,1024));
    printf("FAT stability: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
