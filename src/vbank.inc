/* Selects banks for video cards without a linear framebuffer. */
enum { VB_NONE, VB_CIRRUS, VB_TSENG, VB_S3, VB_TRIDENT };

enum { VBOP_OUT, VBOP_IDX, VBOP_IDX_RD };

typedef struct { u8 op; u16 port; u8 idx, val; } VbWrite;

typedef struct { u32 bank, win_off, src_off, len; } VbPart;

static int vbank_detect(u16 vendor)
{
    if (vendor == 0x1013) return VB_CIRRUS;
    if (vendor == 0x100C) return VB_TSENG;
    if (vendor == 0x5333) return VB_S3;
    if (vendor == 0x1023) return VB_TRIDENT;
    return VB_NONE;
}

static void vb_out(VbWrite *w, u16 port, u8 val)
{
    w->op = VBOP_OUT; w->port = port; w->idx = 0; w->val = val;
}
static void vb_idx(VbWrite *w, u16 port, u8 idx, u8 val)
{
    w->op = VBOP_IDX; w->port = port; w->idx = idx; w->val = val;
}

static int vbank_prog(int scheme, int bank64k, VbWrite *out)
{
    switch (scheme) {
    case VB_CIRRUS:

        vb_idx(out + 0, 0x3C4, 0x06, 0x12);
        vb_idx(out + 1, 0x3CE, 0x0B, 0x00);
        vb_idx(out + 2, 0x3CE, 0x09, (u8)(bank64k << 4));
        return 3;
    case VB_TSENG:

        vb_out(out + 0, 0x3BF, 0x03);
        vb_out(out + 1, 0x3D8, 0xA0);
        vb_out(out + 2, 0x3CD, (u8)((bank64k & 0x0F) * 0x11));
        return 3;
    case VB_S3:

        vb_idx(out + 0, 0x3D4, 0x38, 0x48);
        vb_idx(out + 1, 0x3D4, 0x39, 0xA5);
        vb_idx(out + 2, 0x3D4, 0x35, (u8)(bank64k & 0x0F));
        vb_idx(out + 3, 0x3D4, 0x51, (u8)(((bank64k >> 4) & 3) << 2));
        return 4;
    case VB_TRIDENT:

        out[0].op = VBOP_IDX_RD; out[0].port = 0x3C4; out[0].idx = 0x0B; out[0].val = 0;
        vb_idx(out + 1, 0x3C4, 0x0E, (u8)((bank64k ^ 2) & 0x0F));
        return 2;
    }
    return 0;
}

static inline int vblit_span(int y, int x, int len, int w, int pitch, VbPart out[2])
{
    if (len <= 0) return 0;
    u32 off = (u32)y * (u32)pitch + (u32)x;
    u32 win = off & 0xFFFFu;
    u32 room = 0x10000u - win;
    out[0].bank = off >> 16;
    out[0].win_off = win;
    out[0].src_off = (u32)y * (u32)w + (u32)x;
    if ((u32)len <= room) {
        out[0].len = (u32)len;
        return 1;
    }
    out[0].len = room;
    out[1].bank = out[0].bank + 1;
    out[1].win_off = 0;
    out[1].src_off = out[0].src_off + room;
    out[1].len = (u32)len - room;
    return 2;
}
static inline int vblit_row(int y, int w, int pitch, VbPart out[2])
{
    return vblit_span(y, 0, w, w, pitch, out);
}
