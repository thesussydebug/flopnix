static char c_type[12];

static u8   c_buf[4096];
static u32  c_len;
static u32 c_sequence;
static struct { ClipInfo info; u8 *data; } c_past[5];
static int c_count;

static int clip_set_locked(const char *type, const void *data, u32 n)
{
    if (!type || !type[0] || strlen(type)>=sizeof c_type || n > sizeof c_buf || (n&&!data)) return -1;
    if(c_type[0]&&!strcmp(c_type,type)&&c_len==n&&(!n||!memcmp(c_buf,data,n)))return 0;
    if(c_type[0]){
        u8 *saved=kmalloc(c_len?c_len:1);
        if(saved){
            if(c_len)memcpy(saved,c_buf,c_len);
            if(c_count==5)kfree(c_past[4].data);else c_count++;
            for(int i=c_count-1;i>0;i--)c_past[i]=c_past[i-1];
            c_past[0].data=saved;strlcpy(c_past[0].info.type,c_type,sizeof c_type);
            c_past[0].info.size=c_len;c_past[0].info.sequence=c_sequence;
            mem_track("Clipboard history",saved,c_len?c_len:1);
        }
    }
    strlcpy(c_type, type, sizeof c_type);
    if (n) memcpy(c_buf, data, n);
    c_len = n;
    mem_track("Clipboard",c_buf,sizeof c_buf);
    c_sequence++;
    gui_dirty=1;
    return 0;
}
int clip_set_native(const char *type,const void *data,u32 n)
{
    preempt_disable();int r=clip_set_locked(type,data,n);preempt_enable();return r;
}

static int clip_get_locked(const char *type, void *buf, u32 max)
{
    if (!c_type[0] || (max&&!buf)) return -1;
    if (type && type[0] && strcmp(type, c_type)) return -1;
    u32 n = c_len < max ? c_len : max;
    if (n) memcpy(buf, c_buf, n);
    return (int)n;
}

int clip_get_native(const char *type,void *buf,u32 max)
{
    preempt_disable();int r=clip_get_locked(type,buf,max);preempt_enable();return r;
}

const char *clip_type_native(void) { return c_type[0] ? c_type : 0; }

u32 clip_sequence(void){return c_sequence;}
static int clip_history_locked(int index,ClipInfo *info,void *buf,u32 cap)
{
    if(index<0||index>c_count||!info||!c_type[0]||(cap&&!buf))return -1;
    const u8 *data;
    if(!index){strlcpy(info->type,c_type,sizeof info->type);info->size=c_len;info->sequence=c_sequence;data=c_buf;}
    else{*info=c_past[index-1].info;data=c_past[index-1].data;}
    u32 n=info->size<cap?info->size:cap;if(n)memcpy(buf,data,n);return (int)n;
}
int clip_history(int index,ClipInfo *info,void *buf,u32 cap)
{
    preempt_disable();int r=clip_history_locked(index,info,buf,cap);preempt_enable();return r;
}
int clip_restore(int index)
{
    ClipInfo info;u8 buf[4096];
    int n=clip_history(index,&info,buf,sizeof buf);if(n<0)return -1;
    return clip_set_native(info.type,buf,(u32)n);
}
