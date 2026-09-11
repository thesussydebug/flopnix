/* Builds and checks FLOPFS folder paths. */
static inline int fs_dir_of(const char *name, char *out, int cap)
{
    int i = 0;
    for (; name[i]; i++) {
        if (name[i] != '/') continue;
        if (i >= cap) return 0;
        for (int k = 0; k < i; k++) out[k] = name[k];
        out[i] = 0;
        return 1;
    }
    if (cap > 0) out[0] = 0;
    return 0;
}

static inline int fs_in_dir(const char *name, const char *dir)
{
    int i = 0;
    for (; dir[i]; i++)
        if (name[i] != dir[i]) return 0;
    if (name[i] != '/') return 0;
    if (!name[i + 1]) return 0;

    for (int k = i + 1; name[k]; k++)
        if (name[k] == '/') return 0;
    return 1;
}

static inline int fs_under(const char *name,const char *dir)
{
    int i=0;while(dir[i]){if(name[i]!=dir[i])return 0;i++;}
    return name[i]=='/'&&name[i+1];
}

static inline int fs_child(const char *name,const char *dir,char *out,int cap)
{
    int p=0;if(dir[0]){if(!fs_under(name,dir))return 0;while(dir[p])p++;p++;}
    int end=p;while(name[end]&&name[end]!='/')end++;
    if(end==p||end>=cap)return 0;
    for(int i=0;i<end;i++)out[i]=name[i];out[end]=0;
    return name[end]=='/'?2:1;
}

static inline int fs_rejoin(const char *entry, const char *olddir, const char *newdir,
                     char *out, int cap)
{
    if (!fs_under(entry, olddir)) return 0;
    int od = 0;
    while (olddir[od]) od++;
    const char *base = entry + od + 1;
    int nd = 0;
    while (newdir[nd]) nd++;
    int bl = 0;
    while (base[bl]) bl++;
    if (nd + 1 + bl + 1 > cap) return 0;
    for (int i = 0; i < nd; i++) out[i] = newdir[i];
    out[nd] = '/';
    for (int i = 0; i <= bl; i++) out[nd + 1 + i] = base[i];
    return 1;
}

static inline int fs_name_ok(const char *n)
{
    if (!n || !n[0]) return 0;
    int l = 0;
    while (n[l]) l++;
    return l < FS_NAMELEN;
}

static inline int fs_dirname_ok(const char *n)
{
    if(!n||!n[0])return 0;int begin=0,l=0;
    for(;;l++){
        int c=(unsigned char)n[l];if(l+2>=FS_NAMELEN)return 0;
        if(!c||c=='/'){
            int len=l-begin;
            if(!len||n[begin]==' '||n[l-1]==' '||(n[begin]=='.'&&(len==1||(len==2&&n[begin+1]=='.'))))return 0;
            if(!c)return 1;begin=l+1;
        }else if(c<32||c==127||c=='\\'||c==':')return 0;
    }
}
