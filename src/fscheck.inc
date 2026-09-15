static int fs_table_valid(const FsEnt *entries,int count,u32 first,u32 end)
{
    for(int i=0;i<count;i++){
        const FsEnt *e=&entries[i];if(!e->used)continue;
        int n=0;while(n<FS_NAMELEN&&e->name[n])n++;
        if(e->used!=1||!n||n==FS_NAMELEN)return 0;
        if(e->attr&FS_ATTR_DIR){if(e->size||e->start||e->nsect)return 0;}
        else if(e->start<first||e->start>=end||!e->nsect||e->nsect>end-e->start||e->size>(u32)e->nsect*512)return 0;
        for(int j=0;j<i;j++){
            const FsEnt *b=&entries[j];if(!b->used)continue;
            if(!strcmp(e->name,b->name))return 0;
            if(e->nsect&&b->nsect&&e->start<b->start+b->nsect&&b->start<e->start+e->nsect)return 0;
        }
    }
    return 1;
}
