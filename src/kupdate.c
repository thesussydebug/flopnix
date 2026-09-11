/* Installs a kernel update on the boot floppy. */
#include "os.h"

static u8 sbuf[512];

static void upd_panel(const char *msg, int frac256)
{
    busy_set("Kernel update", msg, frac256);
}

volatile int kupd_critical;

int kernel_update_data(const u8 *img, u32 size, char *err, int errcap)
{
#define FAILU(s) do { strlcpy(err, s, errcap); busy_end(); \
                      kupd_critical = 0; return -1; } while (0)
    if (size == 0 || (size & 511)) FAILU("not a kernel image (size)");
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

    /* Suspend watchdog intervention while replacing the kernel on disk. */
    kupd_critical = 1;
    for (u32 i = 0; i < sect; i++) {
        if (fdc_write(1 + i, img + i * 512))
            FAILU("write error - REFLASH before reboot");
        if ((i & 7) == 0) upd_panel("installing - do not remove the disk",
                                    (int)(i * 256 / sect));
    }
    if (fdc_read(0, sbuf)) FAILU("boot sector error - REFLASH before reboot");
    sbuf[506] = (u8)(sect & 0xFF);
    sbuf[507] = (u8)(sect >> 8);
    if (fdc_write(0, sbuf)) FAILU("boot sector error - REFLASH before reboot");

    upd_panel("installed - rebooting", 256);
    if (timer_alive) {
        u32 t0 = ticks;
        while ((u32)(ticks - t0) < 150) hlt();
    }
    reboot();
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
