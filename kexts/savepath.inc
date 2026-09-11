/* Builds save paths within the file-name limit. */
enum { SAVE_PATH_OK, SAVE_PATH_LONG, SAVE_PATH_BAD };
static inline int save_path_char(int c) { return c >= 32 && c < 127; }
static inline int save_path(int drive, const char *dir, const char *typed,
                           const char *ext, char *out, int cap)
{
    char name[128], path[256];
    int n=0, len=0, rooted=0, tagged=0, slash=0;
    if(cap>0)out[0]=0;
    if(!typed || !*typed)return SAVE_PATH_BAD;
    if(typed[0] && typed[1]==':') {
        if(sh_ci_eq(typed[0],'a'))drive=0;
        else if(sh_ci_eq(typed[0],'u'))drive=1;
        else return SAVE_PATH_BAD;
        typed+=2; tagged=1;
    }
    for(;*typed;typed++) {
        if(n>=127)return SAVE_PATH_LONG;
        char c=*typed=='\\' ? '/' : *typed;
        if(c==':' || (unsigned char)c<32)return SAVE_PATH_BAD;
        if(c=='/')slash=1;
        name[n++]=c;
    }
    name[n]=0;
    const char *leaf=name;
    rooted=tagged || name[0]=='/' || (!drive && slash);
    while(*leaf=='/')leaf++;
    if(!*leaf)return SAVE_PATH_BAD;
    if(drive)path[len++]='/';
    if(!rooted && dir && *dir) {
        while(*dir=='/')dir++;
        while(*dir) {
            if(len>=254)return SAVE_PATH_LONG;
            path[len++]=*dir++;
        }
        if(len && path[len-1]!='/')path[len++]='/';
    }
    while(*leaf) {
        if(len>=254)return SAVE_PATH_LONG;
        path[len++]=*leaf++;
    }
    path[len]=0;

    int start=drive?1:0;
    for(int i=start;i<=len;i++)if(i==len || path[i]=='/') {
        int size=i-start;
        if(!size || (size==1 && path[start]=='.') ||
           (size==2 && path[start]=='.' && path[start+1]=='.'))return SAVE_PATH_BAD;
        start=i+1;
    }
    int e=0;while(ext && ext[e])e++;
    int same=e && len>e && path[len-e-1]=='.';
    for(int i=0;same && i<e;i++)if(!sh_ci_eq(path[len-e+i],ext[i]))same=0;
    if(e && !same) {
        if(len+e+1>=256)return SAVE_PATH_LONG;
        path[len++]='.';
        for(int i=0;i<e;i++)path[len++]=ext[i];
        path[len]=0;
    }
    if((!drive && len>=FS_NAMELEN) || len+3>cap)return SAVE_PATH_LONG;
    out[0]=drive?'u':'a';out[1]=':';
    for(int i=0;i<=len;i++)out[i+2]=path[i];
    return SAVE_PATH_OK;
}
