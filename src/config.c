/* Loads and saves the settings sector. */
#include "os.h"
#include "cfgsan.inc"
#include "prefs.inc"

u8 cfg_was_blank;
static Mutex pref_mutex;

void config_load(void)
{
    FCfg *c = CFG;
    if (c->magic != CFG_MAGIC) {
        cfg_was_blank = 1;
        memset(c, 0, sizeof *c);
        c->magic = CFG_MAGIC;
        c->video = 1;
        c->net_mode = 0;
        c->mouse_speed = 2;
        c->ss_enable = 1;
        c->ss_secs = 30;
        return;
    }
    cfg_sanitize(c);
}

int config_save(void)
{
    CFG->magic = CFG_MAGIC;
    return fdc_write(CFG_LBA, (u8 *)CFG) == 0;
}

int config_get(const char *key,u32 *value)
{
    int i=pref_find(CFG,key);if(i<0||!value)return 0;
    *value=prefs(CFG)->items[i].value;return 1;
}
static int config_set_locked(const char *key,u32 value)
{
    if(!pref_key(key))return 0;
    PrefStore old=*prefs(CFG);
    if(prefs(CFG)->magic!=PREF_MAGIC){memset(prefs(CFG),0,sizeof old);prefs(CFG)->magic=PREF_MAGIC;}
    int i=pref_find(CFG,key);
    if(i<0)for(i=0;i<16&&prefs(CFG)->items[i].key[0];i++);
    if(i>=16)return 0;
    if(prefs(CFG)->items[i].key[0]&&prefs(CFG)->items[i].value==value)return 1;
    strlcpy(prefs(CFG)->items[i].key,key,16);prefs(CFG)->items[i].value=value;
    if(config_save())return 1;
    *prefs(CFG)=old;return 0;
}
int config_set(const char *key,u32 value)
{
    mtx_lock(&pref_mutex);int r=config_set_locked(key,value);mtx_unlock(&pref_mutex);return r;
}
int config_read(void *buf,u32 cap)
{
    if(!buf||cap<sizeof(FCfg))return 0;
    memcpy(buf,CFG,sizeof(FCfg));return sizeof(FCfg);
}
