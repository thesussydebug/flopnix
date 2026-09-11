/* Plays FM notes on the Yamaha OPL2 sound chip. */
#include "kapi.h"
#include "opl2.h"
#include "opl2core.inc"

static const Kapi *api;
static u16 base;
static u8  voice_note[FM_VOICES];
static u8  voice_on[FM_VOICES];
static int master_atten;

static void io_wait(int n)
{
    for (int i = 0; i < n; i++) (void)api->inb(base);
}

static void reg_write(u8 reg, u8 val)
{
    api->outb(base, reg);
    io_wait(6);
    api->outb((u16)(base + 1), val);
    io_wait(35);
}

static void patch_voice(int ch)
{
    u8 m = (u8)opl_op1(ch), c = (u8)opl_op2(ch);
    reg_write((u8)(0x20 + m), 0x21);
    reg_write((u8)(0x20 + c), 0x21);
    reg_write((u8)(0x40 + m), 0x1F);
    reg_write((u8)(0x40 + c), 0x00);
    reg_write((u8)(0x60 + m), 0xF0);
    reg_write((u8)(0x60 + c), 0xF0);
    reg_write((u8)(0x80 + m), 0x0F);
    reg_write((u8)(0x80 + c), 0x0F);
    reg_write((u8)(0xE0 + m), 0x00);
    reg_write((u8)(0xE0 + c), 0x00);
    reg_write((u8)(0xC0 + ch), 0x08);
}

static void chip_reset(void)
{
    for (int r = 0x20; r <= 0xF5; r++) reg_write((u8)r, 0);
    reg_write(0x01, 0x20);
    reg_write(0x08, 0x00);
    reg_write(0xBD, 0x00);
    for (int ch = 0; ch < FM_VOICES; ch++) {
        patch_voice(ch);
        reg_write((u8)(0xB0 + ch), 0x00);
        voice_on[ch] = 0;
        voice_note[ch] = 0;
    }
}

static int detect(u16 port)
{
    base = port;
    reg_write(0x04, 0x60);
    reg_write(0x04, 0x80);
    u8 st1 = api->inb(port);
    reg_write(0x02, 0xFF);
    reg_write(0x04, 0x21);
    io_wait(120);
    u8 st2 = api->inb(port);
    reg_write(0x04, 0x60);
    reg_write(0x04, 0x80);
    if (opl_detected(st1, st2)) return 1;
    base = 0;
    return 0;
}

static const char *fm_name(void) { return "OPL2 (YM3812)"; }
static u16 fm_port(void) { return base; }

static void fm_note_on(int voice, int note, int vel)
{
    if (!base || voice < 0 || voice >= FM_VOICES) return;
    int tl = opl_tl(vel) + master_atten;
    if (tl > 63) tl = 63;

    reg_write((u8)(0xB0 + voice), 0x00);
    reg_write((u8)(0x40 + opl_op2(voice)), (u8)tl);
    int lo;
    int hi = opl_pitch(note, 1, &lo);
    reg_write((u8)(0xA0 + voice), (u8)lo);
    reg_write((u8)(0xB0 + voice), (u8)hi);
    voice_note[voice] = (u8)note;
    voice_on[voice] = 1;
}

static void fm_note_off(int voice)
{
    if (!base || voice < 0 || voice >= FM_VOICES) return;

    int lo;
    int hi = opl_pitch(voice_note[voice], 0, &lo);
    reg_write((u8)(0xB0 + voice), (u8)hi);
    voice_on[voice] = 0;
}

static void fm_all_off(void)
{
    for (int v = 0; v < FM_VOICES; v++) fm_note_off(v);
}

static void fm_set_volume(int atten)
{
    if (atten < 0) atten = 0;
    if (atten > 63) atten = 63;
    master_atten = atten;
}

static const FmOps fm_ops = {
    .abi = FM_ABI,
    .name = fm_name,
    .port = fm_port,
    .note_on = fm_note_on,
    .note_off = fm_note_off,
    .all_off = fm_all_off,
    .set_volume = fm_set_volume,
};

static void on_shutdown(void)
{
    if (base) chip_reset();
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "OPL2 FM"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;

    if (!detect(OPL_PORT) && !detect(0x228)) {
        api->ktrace("fm: no OPL2 at 0x388 or 0x228 - PC speaker only");
        return 0;
    }
    chip_reset();
    char t[64];
    api->kfmt(t, sizeof t, "fm: OPL2 detected at %x, %d voices", base, FM_VOICES);
    api->ktrace(t);
    if (api->register_service("fm", &fm_ops) < 0) return 1;
    api->register_shutdown(on_shutdown);
    return 0;
}
