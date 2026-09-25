/* Loads, relocates, and tracks kernel extensions. */
#include "os.h"
#include "kextspace.inc"
#include "gfxfault.inc"
#include "floppy.h"
#include "debug.h"
#include "panicnet.h"
#include "kexterror.inc"
#include "shellstream.h"
#include "kextfile.h"
extern const ShellStreamOps shell_stream_ops;
extern int fs_replace(const char *,const u8 *,u32);
extern const DebugCore debug_core;

#define MAX_SECT 32

typedef struct {
    u8  ident[16];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Ehdr;
typedef struct {
    u32 name, type, flags, addr, offset, size, link, info, addralign, entsize;
} Shdr;
typedef struct {
    u32 name, value, size;
    u8  info, other;
    u16 shndx;
} Sym;
typedef struct { u32 offset, info; } Rel;

#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHF_ALLOC    2
#define SHN_UNDEF    0
#define SHN_ABS      0xFFF1
#define R_386_32     1
#define R_386_PC32   2

static u32 arena;
static u32 arena_rw;

static KextInfo kexts[FS_NFILES];
typedef struct { u32 address, size, name; } KextSymbol;
typedef struct { u32 count; KextSymbol symbols[]; } KextSymbols;
static KextSymbols *kext_symbols[FS_NFILES];
static u32 kext_ids[FS_NFILES];
static u16 kext_api[FS_NFILES];
static int nkexts;

u32 kext_pool_used;
static u32 kext_priv_phys[FS_NFILES], kext_priv_len[FS_NFILES];
static void *kext_priv_alloc[FS_NFILES];

static int kext_space[FS_NFILES];
static int next_space;

static int space_of(int k) { return k >= 0 ? kext_space[k] - 1 : -1; }
static u8  kext_fixed_logged[FS_NFILES];
static int cur_kext = -1;
static int loading_kext = -1;
static u8 kext_flags[FS_NFILES];
static u32 kext_touched[FS_NFILES];
static int reclaim_one(u32 age);

static u32 range_find(u32 lo,u32 hi,u32 size,u32 align,int priv)
{
    if(!size)return lo;
    for(int pass=0;pass<=FS_NFILES;pass++){
        lo=(lo+align-1)&~(align-1);
        if(lo>hi||size>hi-lo)return 0;
        int clash=0;
        for(int i=0;i<nkexts;i++){
            u32 base=priv?kext_priv_phys[i]:kexts[i].base;
            u32 len=priv?(kext_priv_len[i]+4095)&~4095u:kexts[i].size;
            if(len&&lo<base+len&&base<lo+size){lo=base+len;clash=1;break;}
        }
        if(!clash)return lo;
    }
    return 0;
}

int kext_loading(void) { return loading_kext; }

int kext_owner_now(void) { return ks_owner(loading_kext, cur_kext); }

void kext_enter(int k)
{
    if(k<0){preempt_disable();paging_space_switch(-1);cur_kext=-1;preempt_enable();return;}
    if (k < 0 || k >= nkexts) return;
    kext_touched[k]=ticks;
    if(!kext_priv_len[k]){preempt_disable();paging_space_switch(-1);cur_kext=k;preempt_enable();return;}
    preempt_disable();
    if (k != cur_kext) {
        cur_kext = k;
        int sp = space_of(k);
        if (sp >= 0) {

            paging_space_switch(sp);
        } else {

            paging_space_switch(-1);
            paging_map_kext(kext_priv_phys[k], kext_priv_len[k]);
        }
    }
    preempt_enable();
}

int kext_current(void) { return cur_kext; }

int kext_fix_window(u32 eip, u32 cr2)
{
    for (int k = 0; k < nkexts; k++) {
        if (kexts[k].status || eip < kexts[k].base ||
            eip >= kexts[k].base + kexts[k].size) continue;
        if (k == cur_kext) return 0;
        int sp = space_of(k);

        if (sp >= 0) {
            if (!ks_fixable_slot(sp, cr2, kext_priv_len[k])) return 0;
        } else if (!ks_fixable(cr2, kext_priv_len[k])) return 0;
        cur_kext = k;
        if (sp >= 0) paging_space_switch(sp);
        else { paging_space_switch(-1);
               paging_map_kext(kext_priv_phys[k], kext_priv_len[k]); }

        if (!kext_fixed_logged[k]) {
            kext_fixed_logged[k] = 1;
            char m[64];
            kfmt(m, sizeof m, "window fixed for %s (missed enter)", kexts[k].name);
            klog(m); klog("\n");
            ktrace(m);
        }
        return 1;
    }
    return 0;
}

static int sym_find(const Ehdr *eh, const Shdr *sh, const u8 *img, u32 len,
                    const char *want, const Sym **out_syms, u32 *out_n,
                    const char **out_strs)
{
    int symi = -1;
    for (int i = 0; i < eh->shnum; i++)
        if (sh[i].type == SHT_SYMTAB) { symi = i; break; }
    if (symi < 0 || sh[symi].entsize != sizeof(Sym) ||
        sh[symi].size % sizeof(Sym) || sh[symi].offset > len ||
        sh[symi].size > len-sh[symi].offset)
        return -1;
    const Sym *syms = (const Sym *)(img + sh[symi].offset);
    u32 nsyms = sh[symi].size / sizeof(Sym);
    u32 stri = sh[symi].link;
    if (nsyms > 512 || stri >= eh->shnum || sh[stri].type != 3 ||
        sh[stri].offset > len || sh[stri].size > len-sh[stri].offset) return -1;
    const char *strs = (const char *)(img + sh[stri].offset);
    for(u32 i=0;i<nsyms;i++){
        u32 at=syms[i].name;
        if(at>=sh[stri].size)return -1;
        while(at<sh[stri].size&&strs[at])at++;
        if(at==sh[stri].size)return -1;
    }
    if (out_syms) { *out_syms = syms; *out_n = nsyms; *out_strs = strs; }
    for (u32 i = 0; i < nsyms; i++)
        if (syms[i].shndx != SHN_UNDEF && !strcmp(strs + syms[i].name, want))
            return (int)i;
    return -1;
}

static int elf_sanity(const u8 *img, u32 len, const Ehdr **out_eh,
                      const Shdr **out_sh)
{
    const Ehdr *eh = (const Ehdr *)img;
    if (len < sizeof(Ehdr) || img[0] != 0x7F || img[1] != 'E' ||
        img[2] != 'L' || img[3] != 'F' || eh->ident[4] != 1 ||
        eh->ident[5] != 1 || eh->type != 1   ||
        eh->machine != 3  )
        return 40;
    if (eh->shnum == 0 || eh->shnum > MAX_SECT ||
        eh->shentsize != sizeof(Shdr) ||
        eh->shoff > len || (u32)eh->shnum > (len-eh->shoff)/sizeof(Shdr))
        return 40;
    const Shdr *sh=(const Shdr *)(img+eh->shoff);
    for(u32 i=0;i<eh->shnum;i++)if(sh[i].type!=SHT_NOBITS&&
        (sh[i].offset>len||sh[i].size>len-sh[i].offset))return 40;
    *out_eh = eh;
    *out_sh = (const Shdr *)(img + eh->shoff);
    return 0;
}

static int peek_header(const u8 *img, u32 len, KextHeader *out)
{
    const Ehdr *eh; const Shdr *sh;
    int r = elf_sanity(img, len, &eh, &sh);
    if (r) return r;
    const Sym *syms; u32 nsyms; const char *strs;
    int i = sym_find(eh, sh, img, len, "kext_header", &syms, &nsyms, &strs);
    if (i < 0) return 40;
    const Sym *s = &syms[i];
    if (s->shndx >= eh->shnum) return 40;
    const Shdr *sec = &sh[s->shndx];
    if (sec->type == SHT_NOBITS || s->value>sec->size ||
        sizeof(KextHeader)>sec->size-s->value)
        return 40;
    memcpy(out, img + sec->offset + s->value, sizeof *out);
    if (out->magic != KEXT_MAGIC) return 40;
    int named=0;for(u32 j=0;j<sizeof out->name;j++)if(!out->name[j]){named=1;break;}
    if(!named)return 40;
    if (out->api_version > KAPI_VERSION) return 43;
    if (out->kind != KEXT_KIND_KERNEL && out->kind != KEXT_KIND_APP) return 40;
    return 0;
}

static KextSymbols *save_symbols(const Shdr *sh, u32 nsh, const Sym *syms,
                                u32 nsyms, const char *strs, const u32 *addr)
{
    u16 indices[512];u32 count=0,names=0;
    for(u32 i=0;i<nsyms;i++){
        const Sym *s=&syms[i];
        if((s->info&15)!=2 || s->shndx>=nsh || !addr[s->shndx] ||
           (sh[s->shndx].flags&6)!=6 || s->value>=sh[s->shndx].size || !strs[s->name])continue;
        u32 end=s->name+strlen(strs+s->name)+1;
        if(end>names)names=end;
        indices[count++]=(u16)i;
    }
    u32 at=sizeof(KextSymbols)+count*sizeof(KextSymbol);
    if(!count || names>32768-at)return 0;
    KextSymbols *out=kmalloc(at+names);
    if(!out)return 0;
    out->count=count;
    memcpy((char *)out+at,strs,names);
    for(u32 j=0;j<count;j++){
        const Sym *s=&syms[indices[j]];
        out->symbols[j]=(KextSymbol){addr[s->shndx]+s->value,s->size,at+s->name};
    }
    return out;
}

static int elf_load(const u8 *img, u32 len, u32 *out_base, u32 *out_size,
                    int (**out_entry)(const Kapi *),
                    u32 *out_pphys, u32 *out_plen, void **out_alloc, int *out_space,
                    int hdr_kind, KextSymbols **out_symbols)
{
    const Ehdr *eh; const Shdr *sh;
    int r = elf_sanity(img, len, &eh, &sh);
    if (r) return r;

    u32 shaddr[MAX_SECT];
    for (int i = 0; i < eh->shnum; i++) shaddr[i] = 0;

    u32 code_need=0,code_align=16;
    for(int i=0;i<eh->shnum;i++)if((sh[i].flags&SHF_ALLOC)&&sh[i].size){
        u32 a=sh[i].addralign?sh[i].addralign:1;
        if((a&(a-1))||a>ARENA_END-ARENA_BASE||sh[i].size>ARENA_END-ARENA_BASE+KEXT_PRIV_SIZE)return 40;
        if(!(sh[i].flags&SHF_WRITE_BIT)){if(a>code_align)code_align=a;ks_place(&code_need,sh[i].size,a);}
    }
    u32 base = range_find(ARENA_BASE,arena_rw,code_need,code_align,0);
    if(!base)return 42;
    u32 p = base;
    u32 rw = arena_rw;

    int isolate = paging_active();
    u32 priv_off = 0;
    for (int i = 0; i < eh->shnum; i++)
        if ((sh[i].flags & SHF_ALLOC) && sh[i].size && isolate && ks_is_private(sh[i].flags, hdr_kind))
            ks_place(&priv_off, sh[i].size, sh[i].addralign);
    u32 priv_phys = 0;
    if (priv_off) {
        if(priv_off>KEXT_PRIV_SIZE)return 42;
        u32 pages=(priv_off+4095)&~4095u;
        if(ks_pool_pages_fit(kext_pool_used,priv_off))priv_phys=range_find(KEXT_POOL_BASE,KEXT_POOL_END,pages,4096,1);
        if(priv_phys)kext_pool_used+=pages;
        else{
            *out_alloc=kmalloc(pages+4095);if(!*out_alloc)return 42;
            priv_phys=((u32)*out_alloc+4095)&~4095u;
        }
    }

    int space = -1;
    if (priv_off) {
        int candidate;
        for(candidate=0;candidate<KEXT_PD_MAX;candidate++){
            int used=0;for(int i=0;i<nkexts;i++)if(kext_priv_len[i]&&space_of(i)==candidate){used=1;break;}
            if(!used)break;
        }
        if (candidate < KEXT_PD_MAX && paging_space_create(candidate, priv_phys, priv_off)) {
            space = candidate; if(next_space<=space)next_space=space+1;
            paging_space_switch(space);
        } else {
            paging_space_switch(-1);
            paging_map_kext(priv_phys, priv_off);
        }
    }
    *out_space = space;
    *out_pphys = priv_phys;
    *out_plen  = priv_off;

    u32 pcur = 0;
    for (int i = 0; i < eh->shnum; i++) {
        if (!(sh[i].flags & SHF_ALLOC) || sh[i].size == 0) continue;
        u32 dst;
        if (isolate && ks_is_private(sh[i].flags, hdr_kind)) {
            u32 off = ks_place(&pcur, sh[i].size, sh[i].addralign);
            shaddr[i] = space >= 0 ? ks_priv_va_slot(space, off)
                                   : ks_priv_va(off);

            dst = shaddr[i];
        } else if (sh[i].flags & SHF_WRITE_BIT) {

            u32 a = sh[i].addralign ? sh[i].addralign : 1;
            u32 q = (rw - sh[i].size) & ~(a - 1);
            if (q < p || q < arena || q > rw) return 42;
            rw = q;
            dst = q;
            shaddr[i] = q;
        } else {
            u32 a = sh[i].addralign ? sh[i].addralign : 1;
            p = (p + a - 1) & ~(a - 1);
            if (p + sh[i].size > rw) return 42;
            dst = p;
            shaddr[i] = p;
            p += sh[i].size;
        }
        if (sh[i].type == SHT_NOBITS)
            memset((void *)dst, 0, sh[i].size);
        else {
            if (sh[i].offset > len || sh[i].size > len-sh[i].offset) return 40;
            memcpy((void *)dst, img + sh[i].offset, sh[i].size);
        }
    }

    const Sym *syms; u32 nsyms; const char *strs;
    if (sym_find(eh, sh, img, len, "kext_entry", &syms, &nsyms, &strs) < 0)
        return 41;
    u32 symaddr[512];
    if (nsyms > 512) return 40;
    for (u32 i = 0; i < nsyms; i++) {
        const Sym *s = &syms[i];
        if (s->shndx == SHN_ABS) symaddr[i] = s->value;
        else if (s->shndx == SHN_UNDEF) {
            if (i && strs[s->name])
                return 41;
            symaddr[i] = 0;
        } else if (s->shndx < eh->shnum) {
            if(s->value>sh[s->shndx].size||s->size>sh[s->shndx].size-s->value)return 40;
            symaddr[i] = shaddr[s->shndx] + s->value;
        }
        else return 41;
    }

    for (int i = 0; i < eh->shnum; i++) {
        if (sh[i].type != SHT_REL) continue;
        u32 target = sh[i].info;
        if (target >= eh->shnum || !shaddr[target]) continue;
        if (sh[i].offset > len || sh[i].size > len-sh[i].offset ||
            sh[i].entsize != sizeof(Rel) || sh[i].size % sizeof(Rel))
            return 40;
        const Rel *r2 = (const Rel *)(img + sh[i].offset);
        for (u32 n = sh[i].size / sizeof(Rel); n; n--, r2++) {
            u32 sym = r2->info >> 8, type = r2->info & 0xFF;
            if (sym >= nsyms) return 40;
            if(r2->offset>sh[target].size||sh[target].size-r2->offset<4)return 40;
            u32 *where = (u32 *)(shaddr[target] + r2->offset);
            if (type == R_386_32)        *where += symaddr[sym];
            else if (type == R_386_PC32) *where += symaddr[sym] - (u32)where;
            else return 41;
        }
    }

    for (u32 i = 0; i < nsyms; i++)
        if (syms[i].shndx != SHN_UNDEF &&
            !strcmp(strs + syms[i].name, "kext_entry")) {
            if(syms[i].shndx>=eh->shnum||!shaddr[syms[i].shndx]||
               !(sh[syms[i].shndx].flags&4)||syms[i].value>=sh[syms[i].shndx].size)return 40;
            *out_entry = (int (*)(const Kapi *))symaddr[i];
            *out_base = base;
            *out_size = p - base;
            if(p>arena)arena=p;
            arena_rw = rw;
            *out_symbols=save_symbols(sh,eh->shnum,syms,nsyms,strs,shaddr);
            return 0;
        }
    return 41;
}

extern void fault_show_banner(const char *msg);
#include "lazy.inc"

static int kext_load_locked(const char *name);
static void drop_load_hooks(int owner);
static Mutex load_mutex;
extern u8 gui_up;
static void kext_report(const char *name,int status)
{
    if(!status)return;
    unsigned required=0;
    for(int i=0;i<nkexts;i++)if(!strcmp(kexts[i].name,name)){required=kext_api[i];break;}
    char text[128];kext_error_text(text,sizeof text,name,status,required,KAPI_VERSION);
    klog(text);klog("\n");
    if(gui_up)fault_show_banner(text);
}

int kext_load(const char *name)
{

    char path[FS_NAMELEN];
    if (!name || strlen(name) >= sizeof path) return 40;
    strlcpy(path, name, sizeof path);
    mtx_lock(&load_mutex);
    if (loading_kext >= 0) { mtx_unlock(&load_mutex); return 40; }
    for (int i=0;i<nkexts;i++)
        if (kexts[i].status==46 && !strcmp(kexts[i].name,path)) {
            kexts[i].status=0; mtx_unlock(&load_mutex); return 0;
        }
    for (int i = 0; i < nkexts; i++)
        if (!strcmp(kexts[i].name, path) && (!kexts[i].status || kexts[i].status == 44 || kexts[i].status == 45)) {
            int r = kexts[i].status;
            kext_report(path,r);
            mtx_unlock(&load_mutex); return r;
        }
    int r = kext_load_locked(path);
    kext_report(path,r);
    mtx_unlock(&load_mutex);
    return r;
}

static int kext_load_locked(const char *name)
{
    int slot = nkexts;
    for (int i = 0; i < nkexts; i++)
        if (kexts[i].status && !strcmp(kexts[i].name, name)) { slot = i; break; }
    if (slot >= FS_NFILES) return 42;
    int resident = cur_kext, pd_save = paging_space_current();

    u8 *image = iobuf;
    u32 cap = IOBUF_SZ;
    if (gui_up) {
        cap = 0;
        for (int i = 0; i < FS_NFILES; i++) {
            FsEnt *e = fs_slot(i);
            if (e->used && !strcmp(e->name, name)) { cap = e->size; break; }
        }
        if (!cap || cap > IOBUF_SZ) return 40;
        image = kmalloc(cap);
        if(!image){preempt_disable();while(!image&&reclaim_one(0))image=kmalloc(cap);preempt_enable();}
        if (!image) return 42;
    }
    int n = fs_read(name, image, cap);
    KextHeader hdr;
    int r = n < (int)sizeof(Ehdr) ? 40 : peek_header(image, (u32)n, &hdr);
    kext_api[slot]=!r||r==43?hdr.api_version:0;
    u32 base = 0, size = 0, pphys = 0, plen = 0;
    void *priv_alloc=0;
    int (*entry)(const Kapi *) = 0;
    u32 arena_save = arena, rw_save = arena_rw, pool_save = kext_pool_used;
    int ns_save = next_space;
    int space = -1, li = lazy_file(name);
    preempt_disable();
    paging_arena_protect(ARENA_BASE, ARENA_END, 1);
    if (!r) for(;;){
        arena_save=arena;rw_save=arena_rw;pool_save=kext_pool_used;ns_save=next_space;
        r=elf_load(image,(u32)n,&base,&size,&entry,&pphys,&plen,&priv_alloc,&space,hdr.kind,&kext_symbols[slot]);
        if(!r)break;
        arena=arena_save;arena_rw=rw_save;kext_pool_used=pool_save;next_space=ns_save;
        if(space>=0)paging_space_drop(space);
        if(priv_alloc){kfree(priv_alloc);priv_alloc=0;}
        paging_space_switch(pd_save);cur_kext=resident;
        if(r!=42||!reclaim_one(0))break;
        space=-1;
    }
    if(!r)kext_ids[slot]=crc32(image,(u32)n);
    if (image != iobuf) kfree(image);
    if(r==42)debug_event(DBG_ALLOC,name,cap,0,-1);
    if (r) {
        arena = arena_save; arena_rw = rw_save; kext_pool_used = pool_save;
        next_space = ns_save;
    } else {
        kext_priv_phys[slot] = pphys; kext_priv_len[slot] = plen;
        kext_space[slot] = space + 1;
        kext_priv_alloc[slot]=priv_alloc;
        if(priv_alloc)mem_track(hdr.name,priv_alloc,((plen+4095)&~4095u)+4095);
        kext_flags[slot]=hdr.api_version>=33?(hdr.pad&KEXT_RECLAIMABLE):0;kext_touched[slot]=ticks;
    }
    KextInfo *k = &kexts[slot];
    if (slot == nkexts) nkexts++;
    strlcpy(k->name, name, sizeof k->name);
    strlcpy(k->hname, r ? "?" : hdr.name, sizeof k->hname);
    k->kind = r ? 0 : hdr.kind; k->base = r ? 0 : base;
    k->size = r ? 0 : size; k->status = r;
    if (!r) {
        cur_kext = slot; loading_kext = slot;
        if (li >= 0 && lazy_types[li]) {
            lazy_register_type = lazy_types[li] - 1; lazy_state[li] = 1;
        }
        volatile int rc = 1;
        FAULT_GUARD(rc = entry(&kapi), klog("extension entry faulted\n"));
        loading_kext = -1; lazy_register_type = -1;
        r = rc ? 44 : 0; k->status = r;

        if (r) drop_load_hooks(slot);
        if (li >= 0) lazy_state[li] = r ? 0 : 2;
    }
    paging_space_switch(pd_save); cur_kext = resident;
    paging_arena_protect(ARENA_BASE, arena & ~0xFFFu, 0);
    preempt_enable();
    return r;
}

static int ends_kx(const char *s)
{
    u32 l = strlen(s);
    return l > 3 && s[l - 3] == '.' && s[l - 2] == 'k' && s[l - 1] == 'x';
}

static int file_kind(const char *name)
{
    int n = fs_read(name, iobuf, IOBUF_SZ);
    if (n < (int)sizeof(Ehdr)) return -1;
    KextHeader hdr;
    if (peek_header(iobuf, (u32)n, &hdr) != 0) return -1;
    return hdr.kind;
}

static void boot_pass(int kind, const char *kindname)
{
    for (int i = 0; i < FS_NFILES; i++) {
        FsEnt *e = fs_slot(i);
        if (!e->used || (e->attr & FS_ATTR_DIR) || !ends_kx(e->name)) continue;

        if (!strncmp(e->name, "desktop/", 8)) continue;
        if (lazy_file(e->name) >= 0) continue;
        int k = file_kind(e->name);
        if (k != kind && !(kind == KEXT_KIND_APP && k < 0)) continue;
        boot_print("kext ");
        boot_print(e->name);
        boot_print(" (");
        boot_print(k < 0 ? "?" : kindname);
        boot_print(") ");
        int r = kext_load(e->name);
        if (r == 0) boot_print("ok\n");
        else {
            char c[8];
            kfmt(c, sizeof c, "E%d", r);
            boot_fail(c);
            char detail[128];unsigned required=0;
            for(int j=0;j<nkexts;j++)if(!strcmp(kexts[j].name,e->name)){required=kext_api[j];break;}
            kext_error_text(detail,sizeof detail,e->name,r,required,KAPI_VERSION);
            boot_print(detail);boot_print("\n");
        }
    }
}

int boot_shift;

void kext_boot(void)
{
    arena = ARENA_BASE; arena_rw = ARENA_END;
    if (!fs_ensure()) { boot_print("extensions: no filesystem\n"); return; }

    u8 sc; int skip = 0;
    while (kbd_pop(&sc)) {
        if ((sc & 0x7F) == 0x25) skip = 1;

        if ((sc & 0x7F) == 0x2A || (sc & 0x7F) == 0x36) boot_shift = 1;
    }
    if (skip) { boot_print("extensions SKIPPED (K held)\n"); return; }

    boot_pass(KEXT_KIND_KERNEL, "kernel");
    lazy_catalog();
    if (lazy_present("sys/tmp/notes.lst") || lazy_present("notes.lst")) { int nt = app_find("Notes"); if (nt >= 0) app_ensure_loaded(nt); }
    boot_pass(KEXT_KIND_APP, "app");
    fs_cache_clear();

    if (!nkexts) boot_print("extensions: none on disk\n");

    char m[80];
    kfmt(m, sizeof m, "arena: code %xh..%xh read-only, data %xh..%xh rw",
         ARENA_BASE, arena & ~0xFFFu, arena_rw, ARENA_END);
    klog(m); klog("\n");
    ktrace(m);

    int shared = 0;
    for (int i = 0; i < nkexts; i++)
        if (!kexts[i].status && kext_priv_len[i] && space_of(i) < 0) shared++;
    kfmt(m, sizeof m, "spaces: %d apps with a private page directory "
         "(%xh+, 4MB each), %d on the shared window",
         next_space, KEXT_PRIV_VA, shared);
    klog(m); klog("\n");
    ktrace(m);
}

int kext_count(void) { return nkexts; }
const KextInfo *kext_get(int i) { return (i >= 0 && i < nkexts) ? &kexts[i] : 0; }

static int kext_index_at(u32 eip)
{
    for (int i = 0; i < nkexts; i++)
        if (eip >= kexts[i].base && eip-kexts[i].base < kexts[i].size)
            return i;
    return -1;
}

const char *kext_at(u32 eip)
{
    int i=kext_index_at(eip);
    return i<0 ? 0 : kexts[i].name;
}

void fault_symbol(u32 address, char *out, int cap)
{
    if(!out || cap<=0)return;
    u32 flags=irq_save();
    int i=kext_index_at(address);
    if(i>=0){
        KextSymbols *syms=kext_symbols[i];
        const KextSymbol *best=0;
        if(syms)for(u32 j=0;j<syms->count;j++){
            const KextSymbol *s=&syms->symbols[j];
            if(address>=s->address && (address-s->address<s->size || address==s->address) &&
               (!best || s->address>best->address))best=s;
        }
        if(best){
            char name[60];const char *s=(const char *)syms+best->name;
            strlcpy(name,s,sizeof name);
            if(strlen(s)>=sizeof name)name[sizeof name-2]='~';
            kfmt(out,cap,"%s!%s+0x%x",kexts[i].name,name,address-best->address);
        }else kfmt(out,cap,"%s+0x%x",kexts[i].name,address-kexts[i].base);
    }else{
        extern char __load_end[];
        if(address>=0x8000 && address<(u32)__load_end)kfmt(out,cap,"kernel+0x%x",address-0x8000);
        else kfmt(out,cap,"unknown@0x%08x",address);
    }
    irq_restore(flags);
}

void fault_snapshot(FaultRec *r)
{
    extern char __load_end[];
    fault_symbol(r->eip,r->location,sizeof r->location);
    int i=kext_index_at(r->eip);
    if(i>=0){
        strlcpy(r->module,kexts[i].name,sizeof r->module);
        r->module_base=kexts[i].base;r->module_id=kext_ids[i];
    }else if(r->eip>=0x8000 && r->eip<(u32)__load_end){
        strlcpy(r->module,"kernel",sizeof r->module);
        r->module_base=0x8000;r->module_id=*(const u32 *)0x8004;
    }
    strlcpy(r->owner,r->module[0]?path_base(r->module):"unknown",sizeof r->owner);
}

#define MAX_CMDS 16
static struct {
    const char *name, *usage;
    void (*fn)(const char *args);
    int owner;
} cmds[MAX_CMDS];
static int ncmds;

int register_cmd(const char *name, const char *usage, void (*fn)(const char *))
{
    if (ncmds >= MAX_CMDS || !name || !fn) return -1;
    cmds[ncmds].name = name;
    cmds[ncmds].usage = usage;
    cmds[ncmds].fn = fn;
    cmds[ncmds].owner = ks_owner(loading_kext, cur_kext);
    ncmds++;
    return 0;
}

const char *cmd_usage(const char *name)
{
    if (!lazy_hook(name, 0)) return 0;
    for (int i = 0; i < ncmds; i++)
        if ((cmds[i].owner<0 || !kexts[cmds[i].owner].status) && !strcmp(cmds[i].name, name)) return cmds[i].usage;
    return 0;
}

int cmd_dispatch(const char *name, const char *args)
{
    if (!lazy_hook(name, 0)) return 1;
    for (int i = 0; i < ncmds; i++) {
        if (strcmp(cmds[i].name, name) || (cmds[i].owner>=0 && kexts[cmds[i].owner].status)) continue;
        int resident = kext_current();
        int cpu_prev = cpu_context(app_type_owned(cmds[i].owner));
        kext_enter(cmds[i].owner);
        FAULT_GUARD(cmds[i].fn(args), klog("command faulted\n"));
        cpu_context(cpu_prev);
        kext_enter(resident);
        return 1;
    }
    return 0;
}

static void (*shell_fn)(const char *line);
static int shell_owner=-1;
void register_shell(void (*fn)(const char *line)) { shell_fn = fn; shell_owner=kext_owner_now(); }
int shell_fallback(const char *line)
{
    if (!shell_fn) return 0;
    shell_fn(line);
    return 1;
}

#define MAX_OPENERS 8
static struct {
    const char *ext;
    int (*fn)(const char *name, const char *fullpath, const u8 *data, int n);
    int owner;
} openers[MAX_OPENERS];
static int nopeners;

int register_opener(const char *ext,
                    int (*fn)(const char *, const char *, const u8 *, int))
{
    if (nopeners >= MAX_OPENERS || !ext || !fn) return -1;
    openers[nopeners].ext = ext;
    openers[nopeners].fn = fn;
    openers[nopeners].owner = ks_owner(loading_kext, cur_kext);
    nopeners++;
    return 0;
}

int opener_dispatch(const char *name, const char *fullpath,
                    const u8 *data, int n)
{
    {
        char tm[64];
        kfmt(tm, sizeof tm, "file %s", fullpath ? fullpath : name);
        ktrace(tm);
    }
    const char *dot = 0;
    for (const char *p = name; *p; p++)
        if (*p == '.') dot = p;

    if (dot && !strcasecmp(dot,".kx")) {
        char path[FS_NAMELEN];
        const char *p=fullpath ? fullpath : name;
        if (p[0] && p[1]==':') {
            if (p[0]!='a' && p[0]!='A') return -1;
            p+=2;
        } else if (fullpath && p[0]=='/') return -1;
        if (*p=='/') p++;
        if (strlen(p)>=sizeof path) return -1;
        strlcpy(path,p,sizeof path);
        if (kext_load(path)) return -1;
        for (int i=0;i<nkexts;i++) if (!strcmp(kexts[i].name,path)) {
            int t=app_type_owned(i);
            return t<0 ? 0 : (win_open(t)<0 ? -1 : 0);
        }
        return -1;
    }
    if (!lazy_hook(dot ? dot + 1 : "*", 1)) return -1;
    int sel = -1;
    if (dot)
        for (int i = 0; i < nopeners && sel < 0; i++)
            if ((openers[i].owner<0 || !kexts[openers[i].owner].status) && openers[i].ext[0] && !strcasecmp(dot + 1, openers[i].ext))
                sel = i;
    if (sel < 0 && !lazy_hook("*", 1)) return -1;
    if (sel < 0)
        for (int i = 0; i < nopeners && sel < 0; i++)
            if ((openers[i].owner<0 || !kexts[openers[i].owner].status) && !openers[i].ext[0]) sel = i;
    if (sel < 0) return -1;

    if(app_owner_busy(openers[sel].owner))return -1;
    char cp_name[72], cp_full[128];
    strlcpy(cp_name, name ? name : "", sizeof cp_name);
    if (fullpath) strlcpy(cp_full, fullpath, sizeof cp_full);

    int rc = -1;
    int resident = kext_current();
    kext_enter(openers[sel].owner);
    FAULT_GUARD(rc = openers[sel].fn(cp_name, fullpath ? cp_full : 0, data, n),
                { rc = -1; klog("opener faulted - file not opened\n"); });
    kext_enter(resident);
    return rc;
}

#define NTIMERS 16
static struct {
    u32 interval, next;
    void (*fn)(void *);
    void *ctx;
    int owner;
} timers[NTIMERS];
static int timer_owner=-1;
int kext_timer_busy(int owner){return owner>=0&&timer_owner==owner;}

int timer_add(u32 interval, void (*fn)(void *), void *ctx)
{
    if (!fn || !interval) return -1;
    for (int i = 0; i < NTIMERS; i++)
        if (!timers[i].fn) {
            timers[i].interval = interval;
            timers[i].next = ticks + interval;
            timers[i].ctx = ctx;
            timers[i].owner = ks_owner(loading_kext, cur_kext);
            timers[i].fn = fn;
            return i;
        }
    return -1;
}

void timer_del(int id)
{
    if (id < 0 || id >= NTIMERS) return;

    if (timers[id].owner != ks_owner(loading_kext, cur_kext)) return;
    timers[id].fn = 0;
}

static void drop_load_hooks(int owner)
{
    for (int i = ncmds - 1; i >= 0; i--)
        if (cmds[i].owner == owner) { cmds[i] = cmds[--ncmds]; }
    for (int i = nopeners - 1; i >= 0; i--)
        if (openers[i].owner == owner) { openers[i] = openers[--nopeners]; }
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].owner == owner) timers[i].fn = 0;
}

extern void fault_show_banner(const char *msg);
void timers_poll(void)
{
    if (!timer_alive) return;
    int resident = kext_current();
    for (int i = 0; i < NTIMERS; i++) {
        u32 flags=irq_save();
        void (*fn)(void *) = timers[i].fn;
        if(!fn||(i32)(ticks-timers[i].next)<0||app_owner_busy(timers[i].owner)){irq_restore(flags);continue;}
        timer_owner=timers[i].owner;
        timers[i].next = ticks + timers[i].interval;
        irq_restore(flags);
        int cpu_prev = cpu_context(app_type_owned(timers[i].owner));
        kext_enter(timers[i].owner);
        FAULT_GUARD(fn(timers[i].ctx), ({
            char msg[128];
            const FaultRec *fault = fault_get(0);
            kfmt(msg, sizeof msg, "P%u %s - timer stopped",
                 fault_vec, fault ? fault->location : "unknown");
            klog(msg);
            if (!fault_fallback[thr_self]) fault_show_banner(msg);
            timers[i].fn = 0;
        }));
        timer_owner=-1;
        cpu_context(cpu_prev);
    }
    kext_enter(resident);
}

#define NKHOOKS 4
static int (*khooks[NKHOOKS])(int k);
static int  khook_owner[NKHOOKS];

int register_key_hook(int (*fn)(int k))
{
    for (int i = 0; i < NKHOOKS; i++)
        if (!khooks[i]) {
            khooks[i] = fn;
            khook_owner[i] = kext_owner_now();
            return 0;
        }
    return -1;
}

void unregister_key_hook(int (*fn)(int k))
{
    for (int i = 0; i < NKHOOKS; i++)
        if (khooks[i] == fn) khooks[i] = 0;
}

int key_hook_dispatch(int k)
{
    int resident = kext_current();
    for (int i = 0; i < NKHOOKS; i++) {
        if (!khooks[i]) continue;
        int (*fn)(int) = khooks[i];
        volatile int ate = 0;
        kext_enter(khook_owner[i]);
        FAULT_GUARD(ate = fn(k), {
            khooks[i] = 0;
            klog("key hook faulted - unregistered\n");
        });
        if (ate) { kext_enter(resident); return 1; }
    }
    kext_enter(resident);
    return 0;
}

#define NSHUT 8
static void (*shut_fns[NSHUT])(void);
static int  shut_owner[NSHUT];

void register_shutdown(void (*fn)(void))
{
    for (int i = 0; i < NSHUT; i++)
        if (!shut_fns[i]) {
            shut_fns[i] = fn;
            shut_owner[i] = kext_owner_now();
            return;
        }
}

void shutdown_run(void)
{
    for (int i = 0; i < NSHUT; i++) {
        if (!shut_fns[i]) continue;
        void (*fn)(void) = shut_fns[i];
        kext_enter(shut_owner[i]);

        FAULT_GUARD(fn(), klog("shutdown hook faulted - skipped\n"));
    }
}

#define NSERV 16
static struct { char name[16]; const void *ops; int owner; } servs[NSERV];
static unsigned gfx_disabled;

static unsigned graphics_bit(const char *name)
{
    return !strcmp(name, "gdi") ? 1u : !strcmp(name, "g3d") ? 2u : 0u;
}

unsigned kext_graphics_fault(u32 vec, u32 eip, u32 addr)
{
    unsigned hit = 0;
    for (int i = 0; i < NSERV; i++) {
        if (!servs[i].ops || servs[i].owner < 0) continue;
        const KextInfo *k = &kexts[servs[i].owner];
        if ((eip >= k->base && eip - k->base < k->size) ||
            (vec == 14 && addr >= k->base && addr - k->base < k->size))
            hit |= graphics_bit(servs[i].name);
    }
    unsigned disable = gf_disable(vec, hit & 1, hit & 2, gfx_disabled);
    gfx_disabled |= disable;
    for (int i = 0; i < NSERV; i++)
        if (servs[i].ops && (graphics_bit(servs[i].name) & disable) && servs[i].owner >= 0)
            kexts[servs[i].owner].status = 45;
    return disable;
}

int register_service(const char *name, const void *ops)
{
    if (!name || !name[0] || !ops) return -1;
    if (graphics_bit(name) & gfx_disabled) return -1;
    for (int i = 0; i < NSERV; i++)
        if (servs[i].ops && !strcmp(servs[i].name, name)) {
            servs[i].ops = ops;
            servs[i].owner = kext_owner_now();
            return 0;
        }
    for (int i = 0; i < NSERV; i++)
        if (!servs[i].ops) {
            strlcpy(servs[i].name, name, sizeof servs[i].name);
            servs[i].ops = ops;
            servs[i].owner = kext_owner_now();
            return 0;
        }
    return -1;
}

#include "manager.h"
extern int win_request_close(int index);

const void *service_get(const char *name)
{
    if (!name || (graphics_bit(name) & gfx_disabled)) return 0;
    static const ManagerOps manager={MANAGER_ABI,win_request_close};
    if(!strcmp(name,"manager.core"))return &manager;
    static const KextFileOps files={KEXT_FILE_ABI,peek_header,fs_replace};
    if(!strcmp(name,"kext.files"))return &files;
    if(!strcmp(name,"shell.stream"))return &shell_stream_ops;
    static const FloppyOps floppy={FLOPPY_ABI,fdc_read_many};
    if(!strcmp(name,"disk.floppy"))return &floppy;
    if(!strcmp(name,"debug.core"))return &debug_core;
    extern const PanicMonitor *panic_monitor;
    extern const u32 *panic_frame;
    extern u32 panic_controls[],panic_tss[],heap_top,grow_base,grow_top;
    extern char klog_buf[],trace_buf[],__bss_end[];
    extern FaultRec fault_hist[];
    extern u8 panic_threads[],panic_buffers[];
    static const PanicCore pc={PANIC_MONITOR_ABI,&panic_monitor,&panic_frame,
        panic_controls,panic_tss,kexts,&nkexts,kext_ids,kext_priv_phys,kext_priv_len,&arena_rw,
        klog_buf,trace_buf,(const u32 *)&memory,&heap_top,&grow_base,&grow_top,
        fault_hist,&fault_recoveries,(u32)__bss_end,panic_threads,panic_buffers,THR_MAX*32};
    if(!strcmp(name,"panic.core"))return &pc;
    for (int i = 0; i < NSERV; i++)
        if (servs[i].ops && !strcmp(servs[i].name, name)) return servs[i].ops;
    return 0;
}

#define NLISTEN 16
static void (*listeners[NLISTEN])(const char *event, const char *data);
static int lis_owner[NLISTEN];

int on_event(void (*fn)(const char *, const char *))
{
    if (!fn) return -1;
    for (int i = 0; i < NLISTEN; i++)
        if (!listeners[i]) { listeners[i] = fn; lis_owner[i] = ks_owner(loading_kext, cur_kext); return 0; }
    return -1;
}
void off_event(void (*fn)(const char *, const char *))
{
    for (int i = 0; i < NLISTEN; i++)
        if (listeners[i] == fn) listeners[i] = 0;
}
void broadcast(const char *event, const char *data)
{

    char ev[32], dt[64];
    strlcpy(ev, event ? event : "", sizeof ev);
    strlcpy(dt, data ? data : "", sizeof dt);
    event = ev;
    data = dt;

    int resident = kext_current();

    for (int i = 0; i < NLISTEN; i++)
        if (listeners[i]) {
            kext_enter(lis_owner[i]);

            FAULT_GUARD(listeners[i](event, data ? data : ""), {
                klog("event listener faulted - dropped\n");
                listeners[i] = 0;
            });
        }
    kext_enter(resident);
}

static int can_unload(int owner)
{
    if (owner<0 || owner>=nkexts || (kexts[owner].status && kexts[owner].status!=46)) return -1;
    if (kexts[owner].kind!=KEXT_KIND_APP || (shell_fn && shell_owner==owner) || services_kext_busy(owner) || irq_kext_busy(owner)) return -2;
    for (int i=0;i<NTIMERS;i++) if (timers[i].fn && timers[i].owner==owner) return -2;
    for (int i=0;i<NKHOOKS;i++) if (khooks[i] && khook_owner[i]==owner) return -2;
    for (int i=0;i<NSHUT;i++) if (shut_fns[i] && shut_owner[i]==owner) return -2;
    for (int i=0;i<NSERV;i++) if (servs[i].ops && servs[i].owner==owner) return -2;
    for (int i=0;i<NLISTEN;i++) if (listeners[i] && lis_owner[i]==owner) return -2;
    if (owner==kext_owner_now() || thread_kext_busy(owner)) return -3;
    for (int i=0;i<MAXWIN;i++)
        if (wins[i].used && app_type_owner(wins[i].type)==owner) return -3;
    return 0;
}
static int evictable(int owner)
{
    int li=lazy_file(kexts[owner].name);
    if(li<0||!lazy_types[li]||!(kext_flags[owner]&KEXT_RECLAIMABLE)||!paging_active())return 0;
    int type=lazy_types[li]-1;
    for(int i=0;i<app_count();i++)if(app_type_owner(i)==owner&&i!=type)return 0;
    return 1;
}
static int evict(int owner)
{
    if(!evictable(owner))return 0;
    int li=lazy_file(kexts[owner].name),type=lazy_types[li]-1;
    overlay_drop_owner(owner);
    services_drop_owner(owner);drop_load_hooks(owner);
    AppDesc d={0};d.title=lazy_specs[li].title;d.max_inst=1;d.in_menu=1;
    d.draw=lazy_draw;d.client_size=lazy_size;d.category=lazy_specs[li].category;
    app_placeholder(type,&d);lazy_state[li]=0;
    if(kext_priv_alloc[owner]){kfree(kext_priv_alloc[owner]);kext_priv_alloc[owner]=0;}
    else kext_pool_used-=(kext_priv_len[owner]+4095)&~4095u;
    paging_space_drop(space_of(owner));kext_space[owner]=0;
    kext_priv_len[owner]=kext_priv_phys[owner]=0;kext_fixed_logged[owner]=0;
    kfree(kext_symbols[owner]);kext_symbols[owner]=0;
    kexts[owner].size=kexts[owner].base=0;kexts[owner].status=47;
    gui_dirty=1;return 1;
}
static int reclaim_one(u32 age)
{
    int oldest=-1;
    for(int i=0;i<nkexts;i++)if((kext_flags[i]&KEXT_RECLAIMABLE)&&
        (u32)(ticks-kext_touched[i])>=age&&!can_unload(i)&&evictable(i)){
        if(oldest<0||(u32)(ticks-kext_touched[i])>(u32)(ticks-kext_touched[oldest]))oldest=i;
    }
    return oldest>=0&&evict(oldest);
}
void kext_trim_idle(void)
{
    if(loading_kext>=0||!gui_up)return;
    mtx_lock(&load_mutex);preempt_disable();
    reclaim_one(heap_avail()<65536||KEXT_POOL_END-KEXT_POOL_BASE-kext_pool_used<65536?500:6000);
    preempt_enable();mtx_unlock(&load_mutex);
}
int kext_unload(int owner)
{
    mtx_lock(&load_mutex);preempt_disable();int r=can_unload(owner);
    if(!r&&!evict(owner)){overlay_drop_owner(owner);kexts[owner].status=46;}
    preempt_enable(); mtx_unlock(&load_mutex); return r;
}

static FsEnt *kapi_fs_slot(int i)
{
    return fs_ensure() ? fs_slot(i) : 0;
}
static void kapi_outb(u16 p, u8 v)  { outb(p, v); }
static u8   kapi_inb(u16 p)         { return inb(p); }
static void kapi_outw(u16 p, u16 v) { outw(p, v); }
static u16  kapi_inw(u16 p)         { return inw(p); }
static void kapi_outl(u16 p, u32 v) { outl(p, v); }
static u32  kapi_inl(u16 p)         { return inl(p); }

static void kapi_dirty(void)        { gui_invalidate(); }
static u32  kapi_mem_kb(void)       { return BOOTINFO->mem_kb; }

static u32 kapi_boot_info(int what)
{
    switch (what) {
    case BI_SCREEN_W:   return BOOTINFO->w;
    case BI_SCREEN_H:   return BOOTINFO->h;
    case BI_PITCH:      return BOOTINFO->pitch;
    case BI_BPP:        return BOOTINFO->bpp;
    case BI_VBE_MODE:   return BOOTINFO->vbe;
    case BI_LFB_ADDR:   return BOOTINFO->lfb;
    case BI_MEM_KB:     return BOOTINFO->mem_kb;
    case BI_BOOT_DIAG:  return BOOTINFO->diag;
    case BI_BOOT_STAGE: return BOOTINFO->stage;
    case BI_EBDA_SEG:    return bda_ebda_seg;
    case BI_BASE_MEM_KB: return bda_base_mem_kb;
    }
    return 0;
}

static void kapi_insw(u16 p, void *buf, u32 n)
{
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(p) : "memory");
}

static void kapi_outsw(u16 p, const void *buf, u32 n)
{
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(n) : "d"(p));
}

static void kapi_sleep_ms(u32 ms)
{
    if (timer_alive) {
        u32 t0 = ticks, span = (ms + 9) / 10;
        while ((u32)(ticks - t0) < span) gui_pump();
    } else
        for (volatile u32 i = 0; i < ms * 20000; i++) ;
}

extern char __bss_end[], __bss_start[], __load_end[];

static u32 mem_stat(int what)
{
    switch (what) {
    case MI_TOTAL_KB:     return BOOTINFO->mem_kb;
    case MI_KERNEL_BASE:  return 0x8000u;
    case MI_KERNEL_END:   return (u32)__bss_end;
    case MI_DMA_BASE:     return (u32)DMABUF;
    case MI_DMA_END:      return MEM_DMA_BASE + 65536;
    case MI_FB_BASE:      return (u32)BACKBUF;
    case MI_FB_END:       return (u32)BACKBUF + (u32)SW * SH;
    case MI_STACK_TOP:    return MEM_STACK_TOP;
    case MI_KERNEL_BYTES: return (u32)__load_end - 0x8000u + (u32)__bss_end - (u32)__bss_start;
    case MI_IO_BASE:      return MEM_IO_BASE;
    case MI_IO_END:       return MEM_IO_END;
    case MI_ARENA_BASE:   return ARENA_BASE;
    case MI_ARENA_END:    return ARENA_END;
    case MI_ARENA_RO: {u32 n=0;for(int i=0;i<nkexts;i++)n+=kexts[i].size;return n;}
    case MI_ARENA_RW:     return ARENA_END - arena_rw;
    case MI_POOL_BASE:    return KEXT_POOL_BASE;
    case MI_POOL_END:     return KEXT_POOL_END;
    case MI_POOL_USED:    return kext_pool_used;
    case MI_HEAP_BASE:    return heap_base();
    case MI_HEAP_END:     return heap_end();
    case MI_HEAP_FREE:    return heap_avail();
    case MI_HEAP_LARGEST: return heap_largest();
    case MI_HEAP_BLOCKS:  return heap_blocks();
    case MI_HEAP_GROW_BASE: return heap_grow_base();
    case MI_HEAP_GROW_END: return heap_grow_end();
    case MI_HEAP_CAPACITY: return heap_capacity();
    case MI_HEAP_LIMIT: return heap_limit();
    case MI_PAGING:       return (u32)paging_active();
    case MI_PAGES:        return paging_pages_mapped();
    }
    return 0;
}

Kapi kapi = {
    .version        = KAPI_VERSION,
    .os_version     = OS_VER,

    .fill_rect      = fill_rect,
    .hline          = hline,
    .vline          = vline,
    .bevel          = bevel,
    .panel          = panel,
    .draw_char      = draw_char,
    .draw_text      = draw_text,
    .draw_text_clip = draw_text_clip,
    .draw_text_clip2 = draw_text_clip2,
    .draw_sbar      = draw_sbar,
    .sbar_from_pos  = sbar_from_pos,
    .focus_rect     = focus_rect,
    .blit           = blit,
    .palette_rgb    = palette_rgb,
    .palette_nearest = palette_nearest,
    .screen_w       = &SW,
    .screen_h       = &SH,

    .register_app   = register_app,
    .app_find       = app_find,
    .win_open       = win_open,
    .win_fit_client = win_fit_client,
    .win_is_focused = win_is_focused,
    .mouse_x        = &mx,
    .mouse_y        = &my,
    .gui_blink      = &gui_blink,

    .register_cmd   = register_cmd,
    .shell_print    = shell_print,

    .register_opener = register_opener,
    .open_with      = opener_dispatch,

    .fs_read        = fs_read,
    .fs_write       = fs_write,
    .fs_delete      = fs_delete,
    .fs_exists      = fs_exists,
    .fs_free_kb     = fs_free_kb,
    .fs_slot        = kapi_fs_slot,
    .ext_type       = ext_type,

    .fat_mount      = fat_mount,
    .fat_list       = fat_list,
    .fat_read       = fat_read,
    .fat_write      = fat_write,
    .fat_delete     = fat_delete,
    .fat_writable   = fat_writable,
    .fat_label      = fat_label,
    .fat_total_kb   = fat_total_kb,
    .fat_free_kb    = fat_free_kb,

    .net_up         = net_up,
    .net_dhcp       = net_dhcp,
    .net_ping       = net_ping,
    .net_parse_ip   = net_parse_ip,
    .net_get        = net_get,
    .net_set        = net_set,
    .net_mac        = net_mac_get,

    .usb_present    = usb_present,
    .usb_read       = usb_read,
    .usb_write      = usb_write,
    .usb_capacity_kb = usb_capacity_kb,
    .usb_capacity_sectors = usb_capacity_sectors,
    .usb_model      = usb_model,

    .register_fat   = register_fat,
    .register_net   = register_net,

    .outb           = kapi_outb,
    .inb            = kapi_inb,
    .outw           = kapi_outw,
    .inw            = kapi_inw,
    .outl           = kapi_outl,
    .inl            = kapi_inl,

    .cfg            = CFG,
    .config_save    = config_save,
    .reboot         = reboot,

    .ticks          = &ticks,
    .timer_alive    = &timer_alive,
    .rtc_read       = rtc_read,
    .rtc_now_dos    = rtc_now_dos,
    .dos_fmt        = dos_fmt,

    .kfmt           = kfmt,
    .strlen         = strlen,
    .strcmp         = strcmp,
    .strncmp        = strncmp,
    .strcasecmp     = strcasecmp,
    .strlcpy        = strlcpy,
    .memcpy         = memcpy,
    .memmove        = memmove,
    .memset         = memset,
    .human_size     = human_size,
    .human_size_kb  = human_size_kb,

    .iobuf          = (u8 *)MEM_IO_BASE,
    .iobuf_size     = IOBUF_SZ,

    .register_desktop = register_desktop,
    .clip_set       = clip_set,
    .clip_get       = clip_get,
    .clip_type      = clip_type,
    .menu_show      = menu_show,
    .drag_start     = drag_start,
    .drag_active    = drag_active,
    .set_overlay    = set_overlay,
    .gui_dirty      = kapi_dirty,
    .cpu_brand      = cpu_brand,
    .cpu_mhz        = cpu_mhz,
    .mem_total_kb   = kapi_mem_kb,
    .kext_count     = kext_count,
    .kext_get       = kext_get,

    .pixel          = pixel,
    .getpixel       = getpixel,
    .line           = line,
    .rect           = rect,
    .circle         = circle,
    .fill_circle    = fill_circle,
    .blit_key       = blit_key,
    .read_rect      = read_rect,
    .draw_text_scaled = draw_text_scaled,
    .set_clip       = set_clip,
    .clear_clip     = clear_clip,
    .palette_set    = palette_set,
    .font_glyph     = font_glyph,

    .win_set_title  = win_set_title,
    .win_close_self = win_close_self,
    .win_focus      = win_focus,
    .win_slot       = win_slot,
    .win_max        = win_max,
    .anim_claim     = anim_claim,
    .mouse_buttons  = mouse_buttons,
    .kbd_mods       = kbd_mods,

    .kmalloc        = kmalloc,
    .kfree          = kfree,
    .heap_avail     = heap_avail,

    .timer_add      = timer_add,
    .timer_del      = timer_del,
    .register_key_hook   = register_key_hook,
    .unregister_key_hook = unregister_key_hook,
    .register_shutdown   = register_shutdown,

    .register_service = register_service,
    .service_get    = service_get,

    .boot_info      = kapi_boot_info,
    .cpuid_raw      = cpuid_raw,
    .tsc_read       = tsc_read,
    .pci_cfg_read   = pci_cfg_read,
    .pci_cfg_write  = pci_cfg_write,
    .pci_find       = pci_find,
    .irq_register   = irq_register,
    .irq_unregister = irq_unregister,
    .insw_rep       = kapi_insw,
    .outsw_rep      = kapi_outsw,

    .speaker_tone   = speaker_tone,
    .speaker_off    = speaker_off,

    .sleep_ms       = kapi_sleep_ms,
    .rtc_write      = rtc_write,

    .atoi           = k_atoi,
    .strstr         = k_strstr,
    .strchr         = k_strchr,
    .toupper        = k_toupper,
    .tolower        = k_tolower,
    .ksort          = ksort,
    .rand           = krand,
    .rand_seed      = krand_seed,

    .klog           = klog,
    .klog_read      = klog_read,

    .shell_exec     = shell_exec,

    .kernel_update  = kernel_update,
    .kernel_update_data = kernel_update_data,
    .usb_gen        = usb_generation,

    .msgbox         = msgbox,
    .notify         = notify,
    .file_picker    = file_picker,
    .progress_open  = progress_open,
    .progress_set   = progress_set,
    .progress_close = progress_close,
    .register_dialogs = register_dialogs,
    .clip_set_text  = clip_set_text,
    .clip_get_text  = clip_get_text,
    .broadcast      = broadcast,
    .on_event       = on_event,
    .off_event      = off_event,
    .bmp_load       = bmp_load,
    .text_width     = text_width,
    .font_height    = font_height,
    .text_fit       = text_fit,
    .cursor_hide    = cursor_hide,
    .cursor_shape   = cursor_shape,
    .mouse_warp     = mouse_warp,
    .present        = present,
    .dclick         = dclick,
    .drag_rect_begin = drag_rect_begin,
    .drag_rect_get  = drag_rect_get,
    .uuid_gen       = uuid_gen,
    .path_base      = path_base,
    .path_ext       = path_ext,
    .path_dir       = path_dir,
    .path_join      = path_join,
    .date_fmt       = date_fmt,
    .b64_encode     = b64_encode,
    .b64_decode     = b64_decode,
    .crc32          = crc32,
    .hash_fnv       = hash_fnv,
    .ms_open        = ms_open,
    .ms_alloc       = ms_alloc,
    .ms_free        = ms_free,
    .ms_write       = ms_write,
    .ms_read        = ms_read,
    .ms_seek        = ms_seek,
    .shell_history_count = shell_history_count,
    .shell_history  = shell_history,
    .surface_lock   = surface_lock,
    .surface_unlock = surface_unlock,
    .clip_rect_get  = clip_rect_get,

    .set_overlay_key = set_overlay_key,
    .file_save      = file_save,

    .net_dns        = net_dns,
    .net_http_get   = net_http_get,
    .gui_pump       = gui_pump,

    .term_clear     = term_clear,
    .term_fx        = term_fx,
    .term_hist      = term_hist,
    .win_close      = shell_win_close,
    .disk_read      = fdc_read,
    .fs_defrag      = fs_defrag,
    .register_shell = register_shell,
    .mem_used_kb    = used_kb,
    .cmd_usage      = cmd_usage,
    .dmesg          = dmesg_print,
    .cpu_usage      = cpu_usage,
    .app_count      = app_count,
    .app_desc       = app_desc,
    .kext_load      = kext_load,
    .key_down       = key_is_down,
    .fault_count    = fault_count,
    .fault_get      = fault_get,
    .fs_mkdir       = fs_mkdir,
    .fs_is_dir      = fs_is_dir,
    .fs_rename      = fs_rename,
    .fs_rename_dir  = fs_rename_dir,
    .fs_dir_count   = fs_dir_count,
    .fat_mkdir      = fat_mkdir,
    .fat_rename     = fat_rename,
    .fat_can_mkdir  = fat_can_mkdir,
    .fat_rmdir      = fat_rmdir,
    .busy_set       = busy_set,
    .busy_end       = busy_end,
    .os_build_date  = OS_BUILD_DATE,
    .mem_mapped     = paging_mapped,
    .threads        = threads_print,
    .ktrace         = ktrace,
    .disk_stat      = fdc_stat,
    .shell_cwd      = shell_cwd_get,
    .shell_set_cwd  = shell_cwd_set,
    .mem_info       = mem_stat,
    .ring3_ready    = ring3_active,
    .ring3_test     = ring3_selftest,
    .fs_touch       = fs_touch,
    .fat_exists     = fat_exists,
    .esc_arm        = esc_arm,
    .esc_pending    = esc_pending,
    .mem_writable   = paging_writable,
    .mem_poke       = mem_poke,
    .win_is_hovered = win_is_hovered,
    .net_sntp       = net_sntp,
    .kext_unload    = kext_unload,
    .config_get = config_get, .config_set = config_set, .config_read = config_read,
    .clip_history = clip_history, .clip_restore = clip_restore, .clip_sequence = clip_sequence,
    .mem_track = mem_track, .mem_buffer = mem_buffer,
    .control_state = control_state, .win_redraw = win_redraw,
    .buffer_lock = app_buffer_lock, .buffer_unlock = app_buffer_unlock,
    .network_lock = app_network_lock, .network_unlock = app_network_unlock,
    .krealloc = krealloc,
    .fault_symbol = fault_symbol,
};
