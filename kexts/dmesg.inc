static void do_dmesg(void)
{
    char buf[96];
    u8 d = (u8)api->boot_info(BI_BOOT_DIAG);
    u32 vbe = api->boot_info(BI_VBE_MODE);
    kfmt(buf, sizeof buf, "boot stage %d/8, diag %02x\n", (int)api->boot_info(BI_BOOT_STAGE), d);
    tprint(buf);
    tprint("loader:  kernel loaded (single-sector reads)\n");
    if (d & BD_CFG_HUNG)       tprint("config:  READ HUNG - watchdog skipped it\n");
    else if (!(d & BD_CFG_OK)) tprint("config:  read failed - using defaults\n");
    else if (*api->cfg_blank)  tprint("config:  blank - using defaults\n");
    else                       tprint("config:  loaded from disk\n");
    kfmt(buf, sizeof buf, "font:    %s\n",
         (d & BD_FONT_HUNG) ? "GRAB HUNG (E01) - text may be blank" : "bios 8x16 ok");
    tprint(buf);
    kfmt(buf, sizeof buf, "memory:  %u MB%s\n", api->boot_info(BI_MEM_KB) / 1024,
         (d & BD_MEM_HUNG) ? " (DETECT HUNG, E02 - assumed 4 MB)" : "");
    tprint(buf);
    kfmt(buf, sizeof buf, "video:   %dx%dx8 %s%s%s\n", SW, SH,
         vbe == 2 ? "VESA banked" : vbe ? "VESA LFB" : "VGA",
         (d & BD_VBE_HUNG) ? " (VESA HUNG, E03 - fell back)" : "",
         (d & BD_VGA_HUNG) ? " (VGA SET HUNG, E04)" : "");
    tprint(buf);
    kfmt(buf, sizeof buf, "a20:     %s\n",
         (d & BD_A20_HUNG) ? "BIOS CALL HUNG (E05) - used controller" : "ok");
    tprint(buf);
    tprint((d & BD_ANY_HANG) ? "watchdog recovered from hung BIOS call(s)\n"
                             : "no BIOS hangs caught\n");
    kfmt(buf, sizeof buf, "timer:   %s\n",
         timer_alive ? "tick alive" : "TICK DEAD (E10) - delays estimated");
    tprint(buf);
    tprint("irqs:   ");
    for (int i = 0; i < 16; i++) {
        if (!api->irq_counts[i]) continue;
        kfmt(buf, sizeof buf, " %d:%u", i, api->irq_counts[i]);
        tprint(buf);
    }
    tputc('\n');
    if (api->boot_errs[0]) {
        kfmt(buf, sizeof buf, "self-check FAILED:%s (see README)\n", api->boot_errs);
        tprint(buf);
    } else tprint("self-check: all ok\n");
    char *kl = api->kmalloc(2048);
    if (kl) {
        if (klog_read(kl, 2048) > 0) {
            tprint("--- kernel log ---\n");
            tprint(kl);
            if (kl[strlen(kl) - 1] != '\n') tputc('\n');
        }
        api->kfree(kl);
    }
    int ov = api->thread_overflowed();
    if (ov >= 0) {
        kfmt(buf, sizeof buf, "thread %d blew its stack (killed)\n", ov);
        tprint(buf);
    }
    char lp[160];
    api->loop_prof(lp, sizeof lp);
    tprint("--- loop (ms) ---\n");
    tprint(lp);
    tputc('\n');
    api->loop_prof_reset();
}
