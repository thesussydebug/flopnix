#define PREF_MAGIC 0x32584650u
typedef struct __attribute__((packed)) { char key[16]; u32 value; } Pref;
typedef struct __attribute__((packed)) { u32 magic; Pref items[16]; } PrefStore;
static PrefStore *prefs(FCfg *cfg) { return (PrefStore *)(cfg->reserved+66); }
static int pref_key(const char *key)
{
    if(!key||!*key)return 0;
    int n=0;for(;key[n];n++)if(n>=15||(u8)key[n]<33||(u8)key[n]>126)return 0;
    return 1;
}
static int pref_find(FCfg *cfg,const char *key)
{
    if(!pref_key(key)||prefs(cfg)->magic!=PREF_MAGIC)return -1;
    for(int i=0;i<16;i++)if(!memcmp(prefs(cfg)->items[i].key,key,strlen(key)+1))return i;
    return -1;
}
