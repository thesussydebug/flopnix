#include "kapi.h"
#include "gdi.h"
#include "notebuf.inc"
#include "notelst.inc"

static const Kapi *api;
static const GdiOps *gfx;
static int my_type = -1;

#define NMAX     MAXINST
#define NOTECAP  1024
#define PADX     8
#define PADY     6
#define LINEH    12
#define COLW     8
#define NOTES_LST "sys/tmp/notes.lst"
#define FLUSH_TICKS 100

typedef struct {
    char text[NOTECAP];
    int  len, car;
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
            t->car = t->len;

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
            notes[i].len = notes[i].car = 0;
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

static void n_draw(Win *w, int cx, int cy, int cw, int ch)
{
    int s = slot_for(w->inst);
    if (s < 0) {
        api->draw_text(cx + PADX, cy + PADY, "No free note slot.", C_BLACK);
        return;
    }
    Note *t = &notes[s];
    t->w = cw; t->h = ch;

    api->fill_rect(cx, cy, cw, ch, C_YELLOW);
    if(storage_failed){api->draw_text_clip(cx+PADX,cy+PADY,"Notes storage is unavailable.",C_MAROON,cw-2*PADX);return;}
    if(t->read_failed){api->draw_text_clip(cx+PADX,cy+PADY,"Unable to read this note.",C_MAROON,cw-2*PADX);return;}

    int cols = (cw - PADX * 2) / COLW;
    if (cols < 1) cols = 1;
    int rows = (ch - PADY * 2) / LINEH;
    int caret_shown = 0;

    api->set_clip(cx + PADX, cy + PADY, cw - PADX * 2, ch - PADY * 2);
    int i = 0, r = 0;
    while (r < rows) {
        int e = nb_line_end(t->text, t->len, i, cols);
        char line[96];
        int m = e - i;
        if (m > (int)sizeof line - 1) m = (int)sizeof line - 1;
        for (int k = 0; k < m; k++) line[k] = t->text[i + k];
        line[m] = 0;
        int ly = cy + PADY + r * LINEH;
        api->draw_text(cx + PADX, ly, line, C_BLACK);

        if (!caret_shown && t->car >= i && t->car <= e) {
            if (*api->gui_blink) {
                int col = t->car - i;

                api->fill_rect(cx + PADX + col * COLW - 1, ly + 1, 2, 15,
                               C_BLACK);
            }
            caret_shown = 1;

        }
        if (e >= t->len) break;
        int nx = nb_next_line(t->text, t->len, i, cols);
        if (nx <= i) break;
        i = nx;
        r++;
    }

    api->set_clip(cx, cy, cw, ch);

    if (!t->len)
        api->draw_text(cx + PADX, cy + PADY, "(type a note)", C_G0 + 5);
}

static void n_key(int inst, int k)
{
    int s = slot_for(inst);
    if (s < 0) return;
    Note *t = &notes[s];
    NoteBuf nb = { t->text, NOTECAP, t->len, t->car };
    if(t->read_failed||storage_failed)return;

    if (k == 3) { api->clip_set_text(t->text); return; }
    else if (k == 22) {
        char clip[NOTECAP];
        if (api->clip_get_text(clip, sizeof clip) <= 0) return;
        for (int i = 0; clip[i]; i++) {
            u8 c = (u8)clip[i];
            if ((c == '\n' || (c >= 32 && c != 127)) && !nb_insert(&nb, c)) break;
        }
    }
    else if (k == K_LEFT)  { if (nb.car > 0) nb.car--; }
    else if (k == K_RIGHT) { if (nb.car < nb.len) nb.car++; }
    else if (k == K_HOME)  { while (nb.car > 0 && t->text[nb.car - 1] != '\n') nb.car--; }
    else if (k == K_END)   { while (nb.car < nb.len && t->text[nb.car] != '\n') nb.car++; }
    else if (k == K_UP || k == K_DOWN) {

        int cols = (t->w - PADX * 2) / COLW;
        if (cols < 1) cols = 1;
        int prev = -1, cur = 0, col = 0;
        while (cur <= nb.len) {
            int e = nb_line_end(t->text, nb.len, cur, cols);
            if (nb.car >= cur && nb.car <= e) { col = nb.car - cur; break; }
            int nx = nb_next_line(t->text, nb.len, cur, cols);
            if (nx <= cur) break;
            prev = cur;
            cur = nx;
        }
        if (k == K_UP && prev >= 0) {
            int e = nb_line_end(t->text, nb.len, prev, cols);
            nb.car = prev + col > e ? e : prev + col;
        } else if (k == K_DOWN) {
            int nx = nb_next_line(t->text, nb.len, cur, cols);
            if (nx > cur && nx <= nb.len) {
                int e = nb_line_end(t->text, nb.len, nx, cols);
                nb.car = nx + col > e ? e : nx + col;
            }
        }
    }
    else if (k == '\b')  { if (!nb_backspace(&nb)) return; }
    else if (k == K_DEL) { if (!nb_delete(&nb)) return; }
    else if (k == '\n' || k == '\r') { if (!nb_insert(&nb, '\n')) return; }
    else if (k >= 32 && k < 127) { if (!nb_insert(&nb, k)) return; }
    else return;

    t->len = nb.len;
    t->car = nb.car;
    if(k!=K_LEFT&&k!=K_RIGHT&&k!=K_HOME&&k!=K_END&&k!=K_UP&&k!=K_DOWN){t->changed=1;touch();}
    api->gui_dirty();
}

static void n_mouse(int inst, int lx, int ly, int ev, int cw, int ch)
{
    (void)cw; (void)ch;
    if (ev != EV_PRESS) return;
    int s = slot_for(inst);
    if (s < 0) return;
    Note *t = &notes[s];

    int cols = (t->w - PADX * 2) / COLW;
    if (cols < 1) cols = 1;
    int want = (ly - PADY) / LINEH;
    if (want < 0) want = 0;
    int col = (lx - PADX) / COLW;
    if (col < 0) col = 0;
    int i = 0, r = 0;
    while (r < want) {
        int nx = nb_next_line(t->text, t->len, i, cols);
        if (nx <= i || nx >= t->len) { i = nx > i ? nx : i; break; }
        i = nx;
        r++;
    }
    int e = nb_line_end(t->text, t->len, i, cols);
    t->car = i + col > e ? e : i + col;
    api->gui_dirty();
}

static void n_open(int inst)
{
    if (!loaded) nl_load();
    if (inst >= 0 && inst < NMAX) inst_of[inst] = -1;
    slot_for(inst);
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
        .open = n_open, .draw = n_draw, .mouse = n_mouse, .key = n_key,
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
