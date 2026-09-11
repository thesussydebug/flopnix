# FLOPFS — the FLOPNIX floppy filesystem

FLOPFS is the filesystem on the boot floppy. It is written from scratch for
FLOPNIX and is **not FAT** — a FLOPNIX floppy cannot be read by Windows, Linux
or macOS without [`tools/fscp.py`](../tools/fscp.py).

The whole implementation is [`src/fs.c`](../src/fs.c), about 190 lines including
the file-type table. This document is the specification.

---

## 1. Why not just use FAT?

FAT12 is the obvious choice for a floppy, and FLOPNIX deliberately does not use
it. The reasoning:

| | FAT12 | FLOPFS |
|---|---|---|
| Allocation | cluster chain in a File Allocation Table | one contiguous run per file |
| Metadata to find a file's bytes | walk the FAT chain, sector by sector | one entry: `start` + `nsect` |
| Directory | root dir + nested subdirectories | one flat 128-entry table |
| Free space | scan the FAT for `0x000` entries | derived from the file table |
| Filename | 8.3, upper-case (LFN needs a patented extension) | 23 chars + NUL, case-preserving |
| Fragmentation | expected and handled | **impossible by design** |
| Code to read a file | chain walking + cluster→LBA arithmetic | `start + n` |
| On-disk structures | boot sector, 2 FATs, root dir, data | superblock, table, data |

The trade is explicit: **FLOPFS gives up fragmentation-tolerance to buy
simplicity.** On a 1.44 MB disk that holds a couple dozen extensions and some
text files, fragmentation is a rare nuisance; on a hard disk it would be fatal.

The payoff is that reading a file is genuinely trivial — no allocation table
lives between you and the data — which matters when the reader is a boot-time
kernel with no memory allocator yet.

### What FLOPFS is *not*

It is **not proprietary** in any legal sense: the format is fully specified
here and in ~190 lines of readable C, unencumbered, nothing reverse-engineered.
It is simply **non-standard** — bespoke to this OS.

---

## 2. On-disk layout

A 1.44 MB floppy is 2880 sectors of 512 bytes. FLOPFS divides it:

```
 LBA        sectors  contents
 ---------  -------  --------------------------------------------------
 0                1  boot sector (with a decoy FAT BPB — see §7)
 1 .. 287       287  kernel image (reserved; not owned by the filesystem)
 288              1  superblock
 289 .. 298      10  file table — 128 entries x 40 bytes = 5120 bytes
 299 .. 2879   2581  file data area (contiguous allocations)
```

Constants, from `src/fs.c`:

```c
#define FS_SUPER 288
#define FS_TABLE 289
#define FS_TSECT 10
#define FS_DATA  299
#define FS_END   2880
#define FS_MAGIC 0x53465046      /* "FPFS" little-endian */
#define FS_VER   2
```

The kernel region is a **reservation, not a file**. The filesystem simply never
allocates below `FS_DATA`, so growing the kernel past 287 sectors would silently
overwrite the superblock — which is why `build.sh` hard-fails if the kernel
exceeds 255 sectors, leaving margin.

## 3. The superblock (LBA 288)

12 meaningful bytes, rest zero:

| offset | size | field | value |
|---|---|---|---|
| 0 | u32 | magic | `0x53465046` = `"FPFS"` |
| 4 | u32 | version | `2` |
| 8 | u32 | entry size | `sizeof(FsEnt)` = `40` |

All three must match or the volume is **reformatted**. Including the entry size
is a deliberate safety catch: if `FsEnt` ever grows, an older disk is wiped
rather than misparsed — a stale layout read as the current one would produce
garbage `start`/`nsect` values and corrupt the disk.

## 4. The file table (LBA 289–298)

128 fixed slots, 40 bytes each, no free list and no ordering. Slot index is not
meaningful; it is just where the entry happens to live.

```c
typedef struct {
    char name[24];    /* NUL-terminated, case-preserving */
    u32  size;        /* exact byte length */
    u32  mtime;       /* packed DOS datetime */
    u16  start;       /* first LBA of the data run */
    u16  nsect;       /* sectors allocated (>= ceil(size/512)) */
    u8   used;        /* 0 = free slot */
    u8   attr;        /* reserved, currently unused */
    u8   pad[2];
} FsEnt;              /* exactly 40 bytes on disk */
```

Notes that matter:

- **`used` is the only liveness flag.** Deleting a file clears `used` and
  rewrites the table; the data sectors are left untouched (see §6, undelete).
- **`size` is exact; `nsect` is rounded up.** The tail of the last sector is
  zero-padded on write.
- **`start`/`nsect` are `u16`.** Fine for a 2880-sector floppy; this format does
  not scale to a hard disk without widening them (and bumping `FS_VER`).
- **`mtime`** is packed DOS datetime (the same encoding FAT uses), produced by
  `rtc_now_dos()`.

## 5. Directories: there are none

FLOPFS is **flat**. The `sys/` "folder" you see in the file manager is a
**naming convention, not a structure** — the file is literally named
`sys/gdi.kx`, slash included, in a 24-byte name field.

Consequences you must internalise:

- The file manager, desktop, and picker synthesise immediate folder rows from
  slash-separated names. Explicit empty folders have an `FS_ATTR_DIR` marker
  entry; members still use full prefixed names. Nested folders are supported,
  with no physical `.`/`..` entries. The desktop is the ordinary `desktop/` folder: its files
  store their own data, and `desktop.lst` is only a legacy migration input.
- Deleting `sys/files.kx` deletes **a file**, not a directory member. There is
  no containment relationship to protect you. (This is exactly how a file
  manager once vanished — see [`sh_is_system_file`](../src/shpath.h).)
- A name may be at most 23 characters **including the prefix**, so
  `sys/minesweeper.kx` (18) is fine but long prefixed names get tight.
- Folder moves and renames include all descendants. They reject cycles,
  occupied destinations, and paths that would truncate children. Folder copies
  preflight names and available space; an I/O or allocation failure can leave
  a partial destination, while the source stays intact. GUI deletion requires
  a folder to be empty.

## 6. Allocation

`alloc(n)` finds the **first contiguous run** of `n` free sectors by walking the
file table, not a bitmap:

```c
for (start = FS_DATA; start + n <= FS_END; ) {
    for each used entry e:
        if (start < e->start + e->nsect && e->start < start + n) {
            start = e->start + e->nsect;   /* jump past the clash, retry */
            clash = 1; break;
        }
    if (!clash) return start;
}
return -1;                                  /* no run that long */
```

This is O(files) per probe and needs no on-disk free map — free space *is*
"whatever no entry claims".

**The fragmentation caveat.** Because files are contiguous, `fs_write` can fail
with `-2` even when the total free space is ample, if no single run is long
enough. `fs_free_kb()` reports the *total*, so free space can look sufficient
while a write still fails. There is no defragmenter; rewriting the disk with
`build.sh` compacts it.

**Rewrite behaviour.** Writing an existing file:

- if the new size needs **≤** the already-allocated `nsect`, the data is written
  in place and only `size`/`mtime` change;
- if it needs **more**, the old entry is freed and a fresh run is allocated —
  which can fail on a fragmented disk *even though the file already existed*.

**Undelete is trivial** and worth knowing: delete only clears `used`. Until a
later allocation reuses the run, the entry's `start`/`nsect`/`size` still
describe intact data, so flipping `used` back to 1 restores the file.

## 7. The boot sector is lying to you

LBA 0 carries a **complete, standard 1.44 MB FAT BPB**: bytes/sector 512,
2 FATs, 224 root entries, media descriptor `0xF0`, sectors/FAT 9. None of it is
real. The final field says so outright:

```asm
db "NONE    "   ; fs type (8) - not actually FAT
```

The BPB exists so BIOSes, floppy controllers and disk utilities recognise a
well-formed 1.44 M disk and boot from it. Nothing ever parses those FAT fields —
FLOPFS starts at LBA 288 and ignores them completely.

## 8. Integrity model

There is essentially none, and that is a conscious choice for a single-user
floppy OS:

- **No journal, no transactions.** `fs_write` writes data sectors first, then
  rewrites the whole table. Power loss between the two leaves allocated-but-
  unreferenced sectors (leak, not corruption). Power loss *during* the table
  write can corrupt up to 128 entries.
- **No checksums on file data.** The *kernel image* is checksummed (boot error
  `E09`), but file contents are not.
- **Errors on write are conservative:** if a data sector fails to write, the
  entry is marked unused and the table flushed, so a half-written file does not
  masquerade as complete.
- **Auto-format on mismatch.** A blank or older-version disk is silently
  formatted on first access. There is no "are you sure" — mounting an
  unrecognised disk *destroys* it. This is fine for the intended use (the disk
  is always a FLOPNIX disk) and merciless otherwise.

## 9. The API

Kernel-internal ([`src/os.h`](../src/os.h)), and exposed to extensions through
the `Kapi` table:

```c
int   fs_ensure(void);                              /* mount or format */
int   fs_exists(const char *name);
int   fs_read(const char *name, u8 *buf, u32 max);  /* -> bytes, or -1 */
int   fs_write(const char *name, const u8 *buf, u32 size);
int   fs_delete(const char *name);
u32   fs_free_kb(void);
FsEnt *fs_slot(int i);                              /* 0..127, raw entry */
```

Return codes for `fs_write`: `0` ok, `-1` bad name / disk error, `-2` no space
(no free slot, or no contiguous run).

Enumeration is done by walking `fs_slot(0..127)` and skipping `!used` — that is
how the file manager, the desktop and the shell's `ls` all list files.

`mounted` is cached tri-state (`0` untried, `1` ok, `-1` disk error), so a dead
drive is not retried on every call.

## 10. Host-side access

[`tools/fscp.py`](../tools/fscp.py) implements this same format in Python and is
the only way to move files in or out from a host:

```
python tools/fscp.py built/flopnix.img myext.kx          # add or replace
python tools/fscp.py built/flopnix.img local.kx as.kx    # store under another name
python tools/fscp.py built/flopnix.img --list            # list
```

`build.sh` uses exactly this to install every `.kx` into `sys/`.

**If you change the on-disk format, you must change `fscp.py` in the same
commit and bump `FS_VER`.** The tool duplicates the layout constants; they are
not shared with the C.

## 11. Limits at a glance

| limit | value | set by |
|---|---|---|
| Volume | 1.44 MB (2880 sectors) | `FS_END` |
| Data area | 2581 sectors ≈ 1290 KB | `FS_END - FS_DATA` |
| Max files | 128 | `FS_NFILES` |
| Max name | 23 chars + NUL (prefix included) | `FS_NAMELEN` |
| Max file size | free contiguous space | — |
| Directories | none (flat, `/` is part of the name) | — |
| Timestamps | mtime only | `FsEnt` |
| Permissions | none | — |

`FS_MAXFILE` (65536) is **not** a filesystem limit — it is the text editor's
in-RAM buffer. Files larger than that open read-only in the editor but are
stored fine.

## 12. Extending it safely

If you modify the format:

1. Bump `FS_VER` in `src/fs.c` — old disks then auto-format instead of being
   misread.
2. Mirror every change in `tools/fscp.py` (`MAGIC`, `VER`, `ENTSZ`, offsets).
3. If `FsEnt` changes size, the entry-size check in the superblock catches stale
   disks automatically — keep that check.
4. Remember the table is exactly 10 sectors: `FS_NFILES * sizeof(FsEnt)` must
   stay ≤ `FS_TSECT * 512` (5120 bytes).
5. Add coverage to `kexts/selftest.c` where the logic is pure (name handling
   lives in [`src/shpath.h`](../src/shpath.h) and is already tested).

The shared Save dialog accepts typed paths such as "desktop/picture.bmp" or
"A:/desktop/picture.bmp", as well as choosing the desktop folder and entering
just "picture". It preserves separators, adds the app's extension, and rejects
paths beyond FLOPFS's one folder level or 23-character full-name limit before
closing. Existing folder names count toward that limit; desktop filenames
therefore have 15 characters available, including the extension. Invalid or
missing folders stay in the dialog for correction. Paint remembers a new save
destination only after the write succeeds.
