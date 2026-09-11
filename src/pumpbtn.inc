/* Tracks mouse buttons while a blocking operation runs. */
typedef struct { u8 held; } PumpBtn;

static inline int pb_main(PumpBtn *p, int btn)
{
    p->held = (u8)btn;
    return btn;
}

static inline int pb_pump(PumpBtn *p, int btn)
{
    p->held = (u8)btn;
    return btn;
}
