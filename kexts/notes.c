#include "kapi.h"
#include "gdi.h"
#include "notebuf.inc"
#include "notelst.inc"

static const Kapi *api;
#include "apptext.h"
static const GdiOps *gfx;
static int my_type = -1;

#define NMAX     MAXINST
#define NOTECAP  1024
#define PADX     8
#define PADY     6
#define LINEH    16
#define COLW     8
#define NOTES_LST "sys/tmp/notes.lst"
#define FLUSH_TICKS 100

typedef struct {
    char text[NOTECAP];
    int  len, car;
    int anchor, selecting, scroll;
    u8   used;
    u8   open;
    u8   read_failed;
    u8   changed;
    int  x, y, w, h;
    char file[NL_NAMEMAX];
} Note;

static Note notes[NMAX];
static u8   loaded;
static u8 storage_failed;
static u8   dirty;
static u32  dirty_t;
static int  inst_of[NMAX];
static int nl_save(void);
static int note_dir(void)
{
    const char *dirs[2]={"sys","sys/tmp"};
    for(int d=0;d<2;d++){
        int found=0;
        for(int i=0;i<FS_NFILES;i++){
            const FsEnt *e=api->fs_slot(i);
            if(e&&e->used&&!api->strcmp(e->name,dirs[d])){if(!(e->attr&FS_ATTR_DIR))return 0;found=1;break;}
        }
        if(!found&&api->fs_mkdir(dirs[d])!=0)return 0;
    }
    return 1;
}
static int note_name(char *name,int cap)
{
    for(int n=1;n<=128;n++){
        api->kfmt(name,cap,"sys/tmp/note%d.txt",n);
        if(api->fs_exists(name))continue;
        int used=0;for(int i=0;i<NMAX;i++)if(notes[i].used&&!api->strcmp(notes[i].file,name))used=1;
        if(!used)return 1;
    }
    name[0]=0;return 0;
}

static void nl_load(void)
{
    loaded = 1;
    char buf[NMAX * 64];
    int n = api->fs_read(NOTES_LST, (u8 *)buf, sizeof buf);
    int legacy=n<0&&!api->fs_exists(NOTES_LST);
    if(legacy)n=api->fs_read("notes.lst",(u8 *)buf,sizeof buf);
    else if(n<6||n>=(int)sizeof buf||api->strncmp(buf+n-6,"\n!end\n",6)){storage_failed=1;return;}
    else n-=6;
    if(n>=(int)sizeof buf){storage_failed=1;return;}
    if (n < 0) {storage_failed=api->fs_exists(NOTES_LST)||api->fs_exists("notes.lst");return;}
    buf[n] = 0;
    int slot = 0;
    char *p = buf;
    while (*p) {
        char *e = p;
        while (*e && *e != '\n') e++;
        char sv = *e;
        *e = 0;
        NoteRec r;
        if (*p&&(*p!='\r'||p[1])) {
            if(slot>=NMAX||!nl_parse(p,&r)){storage_failed=1;return;}
            for(int i=0;i<slot;i++)if(!api->strcmp(notes[i].file,r.file)){storage_failed=1;return;}
            Note *t = &notes[slot];
            t->used = 1;
            t->x = r.x; t->y = r.y; t->w = r.w; t->h = r.h;
            t->open = (u8)r.open;
            api->strlcpy(t->file, r.file, sizeof t->file);
            int m = api->fs_read(t->file, (u8 *)t->text, NOTECAP);
            t->read_failed=m<0||m>=NOTECAP;
            if(m>=NOTECAP)m=NOTECAP-1;
            t->len = m > 0 ? m : 0;
            t->text[t->len] = 0;
            t->car = t->anchor = t->len;

            slot++;
        }
        if (!sv) break;
        p = e + 1;
    }
    if(legacy&&note_dir()){
        char old[NMAX][NL_NAMEMAX];int migrated=1;
        for(int i=0;i<slot;i++)api->strlcpy(old[i],notes[i].file,NL_NAMEMAX);
        for(int i=0;i<slot;i++)if(api->strncmp(notes[i].file,"sys/tmp/",8)){
            char dst[NL_NAMEMAX];
            if(notes[i].read_failed||!note_name(dst,sizeof dst)){migrated=0;break;}
            api->strlcpy(notes[i].file,dst,sizeof notes[i].file);
            notes[i].changed=1;
        }
        if(!migrated||!nl_save())for(int i=0;i<slot;i++)
            api->strlcpy(notes[i].file,old[i],NL_NAMEMAX);
    }
}

static int nl_save(void)
{
    if(storage_failed||!note_dir())return 0;
    char buf[NMAX * 64];
    int o = 0;
    for (int i = 0; i < NMAX; i++) {
        if (!notes[i].used) continue;
        NoteRec r;
        r.x = notes[i].x; r.y = notes[i].y;
        r.w = notes[i].w; r.h = notes[i].h;
        r.open = notes[i].open;
        api->strlcpy(r.file, notes[i].file, sizeof r.file);
        int n = nl_fmt(buf + o, (int)sizeof buf - o - 8, &r);
        if (!n) return 0;
        o += n;
        buf[o++] = '\n';
    }
    for (int i = 0; i < NMAX; i++)
        if (notes[i].used && notes[i].file[0] && notes[i].changed && !notes[i].read_failed) {
            if(api->fs_write(notes[i].file, (const u8 *)notes[i].text, notes[i].len)!=0)return 0;
            notes[i].changed=0;
        }
    api->memcpy(buf+o,"\n!end\n",6);o+=6;
    return api->fs_write(NOTES_LST, (const u8 *)buf, o)==0;
}

static void touch(void)
{
    dirty = 1;
    dirty_t = *api->ticks;
}

static void touch_now(void)
{
    dirty = 1;
    dirty_t = 0;
}

static void notes_tick(void *ctx)
{
    (void)ctx;
    if (!dirty) return;
    if (dirty_t && (u32)(*api->ticks - dirty_t) < FLUSH_TICKS) return;
    if(nl_save())dirty=0;else dirty_t=*api->ticks;
}

static int is_bound(int s)
{
    for (int i = 0; i < NMAX; i++)
        if (inst_of[i] == s) return 1;
    return 0;
}

static int slot_for(int inst)
{
    if (inst < 0 || inst >= NMAX) return -1;
    if (inst_of[inst] >= 0) return inst_of[inst];

    for (int i = 0; i < NMAX; i++)
        if (notes[i].used && !is_bound(i)) {
            notes[i].open = 1;
            inst_of[inst] = i;
            touch_now();
            return i;
        }
    for (int i = 0; i < NMAX; i++)
        if (!notes[i].used) {
            char name[NL_NAMEMAX];if(!note_name(name,sizeof name))return -1;
            notes[i].used = 1;
            notes[i].open = 1;
            notes[i].len = notes[i].car = notes[i].anchor = 0;
            notes[i].selecting = notes[i].scroll = 0;
            notes[i].text[0] = 0;
            notes[i].w = 220; notes[i].h = 150;
            notes[i].x = 60 + i * 24; notes[i].y = 60 + i * 20;
            notes[i].read_failed=0;
            notes[i].changed=1;
            api->strlcpy(notes[i].file,name,sizeof notes[i].file);
            inst_of[inst] = i;
            touch_now();
            return i;
        }
    return -1;
}

static int note_cols(Note *t)
{
    int cols = (t->w - PADX * 2) / COLW;
    return cols > 0 ? cols : 1;
}

static int note_rows(Note *t)
{
    int rows = (t->h - PADY * 2) / LINEH;
    return rows > 0 ? rows : 1;
}

static int note_row(Note *t, int pos, int *col)
{
    int i = 0, row = 0, cols = note_cols(t);
    for (;;) {
        int e = nb_line_end(t->text, t->len, i, cols);
        int nx = nb_next_line(t->text, t->len, i, cols);
        if (pos < nx || nx <= i || (pos == e && (e == t->len || t->text[e] == '\n'))) {
            *col = pos - i; if (*col > e - i) *col = e - i;
            return row;
        }
        i = nx; row++;
    }
}

static int note_pos(Note *t, int row, int col)
{
    int i = 0, cols = note_cols(t);
    if (col < 0) col = 0;
    while (row-- > 0) {
        int nx = nb_next_line(t->text, t->len, i, cols);
        if (nx <= i) break;
        i = nx;
    }
    int e = nb_line_end(t->text, t->len, i, cols);
    return i + col > e ? e : i + col;
}

static void note_reveal(Note *t)
{
    int col, row = note_row(t, t->car, &col), rows = note_rows(t);
    if (row < t->scroll) t->scroll = row;
    if (row >= t->scroll + rows) t->scroll = row - rows + 1;
}

static void n_draw(Win *w, int cx, int cy, int cw, int ch)
{
    int s = slot_for(w->inst);
    if (s < 0) { api->draw_text(cx + PADX, cy + PADY, "No free note slot.", C_BLACK); return; }
    Note *t = &notes[s];
    int resized = t->w != cw || t->h != ch;
    t->w = cw; t->h = ch;
    if (resized) note_reveal(t);
    api->fill_rect(cx, cy, cw, ch, C_YELLOW);
    if(storage_failed){api->draw_text_clip(cx+PADX,cy+PADY,"Notes storage is unavailable.",C_MAROON,cw-2*PADX);return;}
    if(t->read_failed){api->draw_text_clip(cx+PADX,cy+PADY,"Unable to read this note.",C_MAROON,cw-2*PADX);return;}
    int cols = note_cols(t), rows = note_rows(t);
    int cc, cr = note_row(t, t->car, &cc);
    TextEdit text = { t->text, NOTECAP, t->len, t->car, t->anchor };
    api->set_clip(cx + PADX, cy + PADY, cw - PADX * 2, ch - PADY * 2);
    int i = note_pos(t, t->scroll, 0);
    for (int r = 0; r < rows; r++) {
        int e = nb_line_end(t->text, t->len, i, cols);
        int nx = nb_next_line(t->text, t->len, i, cols);
        int ly = cy + PADY + r * LINEH;
        for (int p = i; p < e; p++) {
            int selected = te_selected(&text, p), x = cx + PADX + (p - i) * COLW;
            if (selected) api->fill_rect(x, ly, COLW, LINEH, C_NAVY);
            api->draw_char(x, ly, t->text[p], selected ? C_WHITE : C_BLACK);
        }
        if (nx > e && te_start(&text) < nx && te_end(&text) > e)
            api->fill_rect(cx + PADX + (e - i) * COLW, ly, COLW, LINEH, C_NAVY);
        if (t->car == t->anchor && cr == t->scroll + r && api->win_is_focused(w) && *api->gui_blink)
            api->fill_rect(cx + PADX + cc * COLW, ly, 2, LINEH, C_BLACK);
        if (nx <= i || e >= t->len) break;
        i = nx;
    }
    api->set_clip(cx, cy, cw, ch);
    if (!t->len) api->draw_text(cx + PADX, cy + PADY, "(type a note)", C_G0 + 5);
}

static void n_key(int inst, int k)
{
    int s = slot_for(inst);
    if (s < 0) return;
    Note *t = &notes[s];
    if(t->read_failed||storage_failed)return;
    TextEdit text = { t->text, NOTECAP, t->len, t->car, t->anchor };
    int r;
    if (k == K_UP || k == K_DOWN || k == K_PGUP || k == K_PGDN) {
        int col, row = note_row(t, t->car, &col);
        row += k == K_UP ? -1 : k == K_DOWN ? 1 : k == K_PGUP ? -note_rows(t) : note_rows(t);
        te_move(&text, note_pos(t, row, col), api->kbd_mods() & 1);
        r = 1;
    } else r = at_key(&text, k, 1, 0, 0);
    if (r < 0) api->notify("Text did not fit; original note kept.");
    t->len = text.len; t->car = text.caret; t->anchor = text.anchor;
    if (r == 2) { t->changed = 1; touch(); }
    note_reveal(t);
    api->gui_dirty();
}

static void n_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    int s = slot_for(inst);
    if (s < 0) return;
    Note *t = &notes[s];
    if (ev == EV_RELEASE) { t->selecting = 0; return; }
    if (ev != EV_PRESS && !(ev == EV_DRAG && t->selecting)) return;
    if(t->read_failed||storage_failed)return;
    t->w = cw; t->h = ch;
    if (ev == EV_PRESS) t->selecting = 1;
    int row = (ly - PADY) / LINEH;
    if (ly < PADY) { row = 0; if (t->scroll > 0) t->scroll--; }
    if (row >= note_rows(t)) {
        int col, last = note_row(t, t->len, &col);
        if (t->scroll + note_rows(t) <= last) t->scroll++;
        row = note_rows(t) - 1;
    }
    t->car = note_pos(t, t->scroll + row, (lx - PADX + COLW / 2) / COLW);
    if (ev == EV_PRESS && !(api->kbd_mods() & 1)) t->anchor = t->car;
    api->gui_dirty();
}

static void n_wheel(int inst, int dz)
{
    int s = slot_for(inst), col;
    if (s < 0) return;
    Note *t = &notes[s];
    int last = note_row(t, t->len, &col) - note_rows(t) + 1;
    t->scroll -= dz * 3;
    if (t->scroll > last) t->scroll = last;
    if (t->scroll < 0) t->scroll = 0;
    api->gui_dirty();
}

static void n_open(int inst)
{
    if (!loaded) nl_load();
    if (inst >= 0 && inst < NMAX) inst_of[inst] = -1;
    int s = slot_for(inst);
    if (s >= 0) { notes[s].anchor = notes[s].car; notes[s].selecting = 0; note_reveal(&notes[s]); }
}

static void n_close(int inst)
{
    int s = (inst >= 0 && inst < NMAX) ? inst_of[inst] : -1;
    if (s < 0) return;
    notes[s].open = 0;

    if (!notes[s].len&&!notes[s].read_failed&&!storage_failed) {
        notes[s].used = 0;
        api->fs_delete(notes[s].file);
    }
    inst_of[inst] = -1;
    touch_now();
}

static void n_csize(int inst, int *w, int *h)
{
    int s = (inst >= 0 && inst < NMAX) ? inst_of[inst] : -1;
    *w = (s >= 0 && notes[s].w) ? notes[s].w : 220;
    *h = (s >= 0 && notes[s].h) ? notes[s].h : 150;
}
static void n_min(int *w, int *h) { *w = NL_MINW; *h = NL_MINH; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Notes"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    gfx = gdi_bind(k, 11);
    for (int i = 0; i < NMAX; i++) inst_of[i] = -1;

    static const AppDesc d = {.live_draw=APP_INDEPENDENT,
        .title = "Notes", .max_inst = NMAX, .in_menu = 1, .resizable = 1,
        .open = n_open, .draw = n_draw, .mouse = n_mouse, .wheel = n_wheel, .key = n_key,
        .client_size = n_csize, .min_client = n_min, .close = n_close,
        .category = APP_CAT_PROGRAMS,
    };
    my_type = k->register_app(&d);
    if (my_type < 0) return 1;

    nl_load();
    k->timer_add(25, notes_tick, 0);

    for (int i = 0; i < NMAX; i++)
        if (notes[i].used && notes[i].open && notes[i].len)
            k->win_open(my_type);
    return 0;
}
