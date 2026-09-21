/* Installs a kernel update on the boot floppy. */
#include "os.h"
#include "floppy.h"

static u8 sbuf[512];
static int updating;

static void upd_panel(const char *msg, int frac256)
{
    busy_set("Kernel update", msg, frac256);
}

volatile int kupd_critical;

int kernel_update_data(const u8 *img, u32 size, char *err, int errcap)
{
    u32 flags=irq_save();
    if(updating){irq_restore(flags);strlcpy(err,"kernel update already in progress",errcap);return -1;}
    updating=1;irq_restore(flags);
    u8 *batch=0;
#define FAILU(s) do { strlcpy(err, s, errcap); busy_end(); \
                      kfree(batch);kupd_critical = 0; updating=0; return -1; } while (0)
    if (!img || size == 0 || (size & 511)) FAILU("not a kernel image (size)");
    u32 sect = size / 512;
    if (sect < 8 || sect > 255) FAILU("not a kernel image (size)");
    if (img[0] != 0xEB) FAILU("not a kernel image (no stub)");
    u16 want_sum  = (u16)(img[4] | (img[5] << 8));
    u16 want_sect = (u16)(img[6] | (img[7] << 8));
    if (want_sect != sect) FAILU("bad header (truncated file?)");

    upd_panel("verifying the image...", 0);
    u16 sum = 0;
    for (u32 i = 0; i < sect; i++) {
        const u8 *s = img + i * 512;
        for (int o = 0; o < 512; o += 2) {
            u16 w = (i == 0 && o == 4) ? 0
                    : (u16)(s[o] | (s[o + 1] << 8));
            sum = (u16)(((sum << 1) | (sum >> 15)) + w);
        }
        if ((i & 31) == 0) upd_panel("verifying the image...",
                                     (int)(i * 256 / sect));
    }
    if (sum != want_sum) FAILU("update file is corrupt - re-copy it");
    u8 boot[512];
    if(fdc_read(0,boot))FAILU("cannot read boot sector - kernel unchanged");
    batch=kmalloc(FLOPPY_TRACK_SECTORS*512);
    if(batch)mem_track("Kernel readback",batch,FLOPPY_TRACK_SECTORS*512);
    u8 *verify=batch?batch:sbuf;

    /* Suspend watchdog intervention while replacing the kernel on disk. */
    kupd_critical = 1;
    for (u32 i = 0; i < sect;) {
        u32 n=batch?FLOPPY_TRACK_SECTORS-(1+i)%FLOPPY_TRACK_SECTORS:1;
        if(n>sect-i)n=sect-i;
        if (fdc_write_many(1 + i, img + i * 512,n) || fdc_read_many(1 + i, verify,n) ||
            memcmp(verify, img + i * 512,n*512))
            FAILU("write error - REFLASH before reboot");
        i+=n;
        upd_panel("installing - do not remove the disk",
                                    (int)(i * 256 / sect));
    }
    boot[506] = (u8)(sect & 0xFF);
    boot[507] = (u8)(sect >> 8);
    u8 check[512];
    if (fdc_write(0, boot) || fdc_read(0, check) || memcmp(boot, check, 512))
        FAILU("boot sector error - REFLASH before reboot");
    kfree(batch);

    upd_panel("installed - rebooting", 256);
    if (timer_alive) {
        u32 t0 = ticks;
        while ((u32)(ticks - t0) < 150) hlt();
    }
    reboot();
    kupd_critical=updating=0;
    return 0;
#undef FAILU
}

int kernel_update(const char *name, char *err, int errcap)
{
    if (!fs_ensure()) { strlcpy(err, "A: is not readable", errcap); return -1; }
    int n = fs_read(name, iobuf, IOBUF_SZ);
    if (n < 0) { strlcpy(err, "file not found", errcap); return -1; }
    return kernel_update_data(iobuf, (u32)n, err, errcap);
}
