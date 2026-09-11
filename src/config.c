/* Loads and saves the settings sector. */
#include "os.h"
#include "cfgsan.inc"

u8 cfg_was_blank;

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
