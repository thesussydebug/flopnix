#include "textfilter.inc"
typedef struct {u8 *data;u32 size,cap;int file,error;} SfOutput;
static void sf_emit(SfOutput *o,const void *data,u32 n)
{
    if(o->error)return;
    if(!o->file){const char *p=data;char b[129];while(n){u32 take=n>128?128:n;memcpy(b,p,take);b[take]=0;tprint(b);p+=take;n-=take;}return;}
    if(n>FS_MAXFILE-o->size){o->error=1;return;}
    u32 need=o->size+n;
    if(need>o->cap){u32 cap=o->cap?o->cap:4096;while(cap<need)cap*=2;if(cap>FS_MAXFILE)cap=FS_MAXFILE;
        u8 *p=api->krealloc(o->data,cap);if(!p){o->error=1;return;}o->data=p;o->cap=cap;
    }
    if(n)memcpy(o->data+o->size,data,n);o->size+=n;
}
static void sf_line(SfOutput *o,const u8 *p,u32 n){sf_emit(o,p,n);sf_emit(o,"\n",1);}
static int sf_number(const char *p,u32 *n)
{
    if(!*p)return 0;u32 v=0;for(;*p;p++){if(*p<'0'||*p>'9'||v>((u32)FS_MAXFILE-(*p-'0'))/10)return 0;v=v*10+*p-'0';}*n=v;return 1;
}
static int sf_exec(const char *cmd,const char *args)
{
    const char *names[]={"cat","head","tail","grep","wc","sort","uniq","strings","crc32","cmp"};int kind=-1;
    for(int i=0;i<10;i++)if(!strcmp(cmd,names[i]))kind=i;if(kind<0)return 0;
    char work[192],dest[96],raw[128],pattern[128],path[96],second[96],b[160];
    if(strlen(args)>=sizeof work){tprint("Command is too long.\n");return 1;}strlcpy(work,args,sizeof work);
    int dd=0,append=0,redirect=0,quote=0,bad=0,drive=0,sd=0;const char *to=0;
    for(char *p=work;*p;p++){
        if(quote){if(*p==quote)quote=0;continue;}
        if(*p=='\''||*p=='"'){quote=*p;continue;}
        if(*p=='>'){*p=0;to=p+1;if(*to=='>'){append=1;to++;}redirect=1;break;}
    }
    if(quote)bad=1;
    if(redirect&&(sp_token(&to,raw,sizeof raw)<=0||*to||!sp_resolve(api->shell_cwd(),raw,&dd,dest,sizeof dest)))bad=1;
    int fold=0,invert=0,number=0,count=0,reverse=0,unique=0,numeric=0,flags=0,pos=0,endopts=0;
    u32 limit=kind==7?4:10;
    const char *p=work;
    while(*p&&!bad){int len=sp_token(&p,raw,sizeof raw);if(len<0){bad=1;break;}
        if(!endopts&&!strcmp(raw,"--")){endopts=1;continue;}
        if(!endopts&&raw[0]=='-'&&raw[1]){
            if((kind==1||kind==2||kind==7)&&!strcmp(raw,"-n")){if(sp_token(&p,raw,sizeof raw)<=0||!sf_number(raw,&limit)||(kind==7&&!limit))bad=1;continue;}
            for(int i=1;raw[i];i++){
                char c=raw[i];
                if(kind==3&&c=='i')fold=1;else if(kind==3&&c=='v')invert=1;else if(kind==3&&c=='n')number=1;
                else if((kind==3||kind==6)&&c=='c')count=1;
                else if(kind==5&&c=='r')reverse=1;else if(kind==5&&c=='u')unique=1;else if(kind==5&&c=='n')numeric=1;
                else if(kind==4&&(c=='l'||c=='w'||c=='c'))flags|=c=='l'?1:c=='w'?2:4;
                else bad=1;
            }continue;
        }
        if(kind==3&&pos==0){strlcpy(pattern,raw,sizeof pattern);pos++;continue;}
        int filepos=pos-(kind==3);
        if(filepos==0){if(!sp_resolve(api->shell_cwd(),raw,&drive,path,sizeof path))bad=1;}
        else if(kind==9&&filepos==1){if(!sp_resolve(api->shell_cwd(),raw,&sd,second,sizeof second))bad=1;}
        else bad=1;pos++;
    }
    if(pos!=(kind==3||kind==9?2:1))bad=1;
    if(bad){const char *u=sh_usage(cmd);tprint(u?u:"Invalid arguments");tputc('\n');return 1;}
    if(redirect&&((dd==drive&&!(dd?strcasecmp(dest,path):strcmp(dest,path)))||(kind==9&&dd==sd&&!(dd?strcasecmp(dest,second):strcmp(dest,second))))){tprint("Choose an output path different from the input.\n");return 1;}
    int n=path_read(drive,path,iobuf,FS_MAXFILE);
    if(n<0){kfmt(b,sizeof b,"%s: %s: %s\n",cmd,path,n==-4?"file exceeds 512 KiB":n==-3?"is a folder":"could not read file");tprint(b);return 1;}
    SfOutput out={0};out.file=redirect;
    if(append){u32 old=0;int dir=0;if(path_info(dd,dest,&old,&dir)){
        if(dir||old>FS_MAXFILE){tprint("Cannot append to that destination.\n");return 1;}
        out.data=api->kmalloc(old?old:1);out.cap=old;
        if(!out.data||path_read(dd,dest,out.data,old)<0){if(out.data)api->kfree(out.data);tprint("Could not read the existing output file.\n");return 1;}out.size=old;
    }}
    if(kind==0)sf_emit(&out,iobuf,n);
    else if(kind==4){u32 lines=0,words=0;int inword=0;for(int i=0;i<n;i++){if(iobuf[i]=='\n')lines++;if(tf_space(iobuf[i]))inword=0;else if(!inword){words++;inword=1;}}
        if(!flags){kfmt(b,sizeof b,"%u lines  %u words  %u bytes\n",lines,words,n);sf_emit(&out,b,strlen(b));}
        else {u32 values[]={lines,words,n};for(int i=0;i<3;i++)if(flags&(1<<i)){kfmt(b,sizeof b,"%u ",values[i]);sf_emit(&out,b,strlen(b));}sf_emit(&out,"\n",1);}
    }else if(kind==8){kfmt(b,sizeof b,"%08x  %u bytes  %s\n",tf_crc32(iobuf,n),n,path);sf_emit(&out,b,strlen(b));}
    else if(kind==9){u32 size=0;int dir=0;if(!path_info(sd,second,&size,&dir)||dir||size>FS_MAXFILE){tprint("cmp: could not read the second file.\n");out.error=1;}
        else {u8 *other=api->kmalloc(size?size:1);if(!other){out.error=1;}else{int got=path_read(sd,second,other,size);if(got<0||(u32)got!=size)out.error=1;else{u32 i=0;while(i<(u32)n&&i<size&&iobuf[i]==other[i])i++;
            if(i==(u32)n&&i==size)strlcpy(b,"Files are identical.\n",sizeof b);else kfmt(b,sizeof b,"Files differ at byte %u (offset 0x%x).\n",i+1,i);sf_emit(&out,b,strlen(b));}api->kfree(other);}}
    }else if(kind==7){for(u32 i=0;i<(u32)n;){u32 start=i;while(i<(u32)n&&iobuf[i]>=32&&iobuf[i]<127)i++;if(i-start>=limit)sf_line(&out,iobuf+start,i-start);if(i<(u32)n)i++;}}
    else if(kind==5){u32 total=tf_count(iobuf,n);TfLine *rows=total?api->kmalloc(total*sizeof *rows):0;
        if(total&&!rows)out.error=1;
        else {u32 at=0;for(u32 i=0;i<total;i++){u32 end=tf_end(iobuf,n,at);rows[i].start=at;rows[i].end=end-(end>at&&iobuf[end-1]=='\r');at=end+1;}
            tf_sort(iobuf,rows,total,numeric,reverse);
            for(u32 i=0;i<total;i++){if(unique&&i&&!tf_compare(iobuf,rows[i-1],rows[i],numeric))continue;sf_line(&out,iobuf+rows[i].start,rows[i].end-rows[i].start);if((i&255)==255&&gui_pump()){out.error=1;break;}}
            if(rows)api->kfree(rows);
        }
    }else{
        u32 line=0,hits=0,total=tf_count(iobuf,n),prev=0,prevlen=0,repeats=0;
        for(u32 at=0;at<(u32)n;){u32 end=tf_end(iobuf,n,at),len=end-at;if(len&&iobuf[at+len-1]=='\r')len--;line++;
            if(kind==6){int same=repeats&&len==prevlen;for(u32 i=0;same&&i<len;i++)if(iobuf[at+i]!=iobuf[prev+i])same=0;
                if(!same&&repeats){if(count){kfmt(b,sizeof b,"%u ",repeats);sf_emit(&out,b,strlen(b));}sf_line(&out,iobuf+prev,prevlen);repeats=0;}prev=at;prevlen=len;repeats++;
            }else{int show=kind==1?line<=limit:kind==2?line>(total>limit?total-limit:0):tf_match(iobuf+at,len,pattern,fold)!=invert;
                if(show){hits++;if(kind==1||kind==2)sf_emit(&out,iobuf+at,end-at+(end<(u32)n));else if(!count){if(number){kfmt(b,sizeof b,"%u:",line);sf_emit(&out,b,strlen(b));}sf_line(&out,iobuf+at,len);}}}
            at=end+1;if((line&255)==255&&gui_pump()){out.error=1;break;}
        }
        if(kind==3&&count){kfmt(b,sizeof b,"%u\n",hits);sf_emit(&out,b,strlen(b));}
        if(kind==6&&repeats){if(count){kfmt(b,sizeof b,"%u ",repeats);sf_emit(&out,b,strlen(b));}sf_line(&out,iobuf+prev,prevlen);}
    }
    if(out.error)tprint("Command stopped: not enough memory, output exceeds 512 KiB, or cancelled. No output file was written.\n");
    else if(redirect){int r=dd?fat_write(dest,out.data?out.data:(const u8 *)"",out.size):fs_write(dest,out.data?out.data:(const u8 *)"",out.size);
        if(r)tprint("Could not write the output file.\n");else api->broadcast("fs.changed","");}
    if(out.data)api->kfree(out.data);return 1;
}
