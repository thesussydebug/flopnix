/* Waits for USB connection state to settle. */
#define USB_DB_TICKS 50u

enum { USB_DB_NONE, USB_DB_ARM, USB_DB_ACT, USB_DB_DROP };

static inline int usb_db_step(int pend, u32 pend_tick,
                              int cur, int last, u32 now)
{
    if (pend < 0)
        return cur == last ? USB_DB_NONE : USB_DB_ARM;
    if (cur != pend) return USB_DB_ARM;
    if ((u32)(now - pend_tick) < USB_DB_TICKS) return USB_DB_NONE;
    return cur == last ? USB_DB_DROP : USB_DB_ACT;
}
