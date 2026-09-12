# FLOPNIX architecture

How the system is put together, from power-on to a drawn pixel. This is the
"how does it actually work" companion to [MAINTAINING.md](MAINTAINING.md) (how to
build and change it) and [FLOPFS.md](FLOPFS.md) (the disk format).

FLOPNIX targets any 386 or later with VGA; the reference machine is a ~400 MHz
Pentium II, and QEMU is the test rig.

---

## 1. The shape of the system

```
        +-------------------------------------------------------+
        |  extensions (.kx)   apps: files, edit, paint, clock…   |
        |                     services: gdi, g3d, fat, net,      |
        |                               dialogs, desktop         |
        +----------------------- Kapi -------------------------- +
        |  kernel: shell, window manager, compositor, graphics,   |
        |          FLOPFS, floppy + USB drivers, kext loader      |
        +-------------------------------------------------------+
        |  stub (real mode): font, memory size, A20, video mode   |
        |  boot sector: load kernel + config, show progress       |
        +-------------------------------------------------------+
```

The base kernel is deliberately small. Everything replaceable — filesystems for
removable media, networking, the 2D/3D graphics engines, every application, even
the desktop and the standard dialogs — is a **kernel extension** loaded through a versioned function-pointer table.
Services load at boot; shipped apps are loaded on first use.

## 2. Boot chain

**Boot sector (LBA 0).** Loads LBA 1..256 sequentially — kernel *and* config in
one pass — showing a live `kernel NNN KB` counter. A failed read prints the BIOS
status code and the LBA rather than hanging. It also carries a decoy FAT BPB so
BIOSes accept the disk (see [FLOPFS.md §7](FLOPFS.md#7-the-boot-sector-is-lying-to-you)).

**Real-mode stub.** Does no disk I/O at all. It grabs the BIOS font, queries
memory size (E801), enables **and verifies** A20 (BIOS → keyboard controller →
port 0x92, re-checked from 32-bit mode), and picks a video mode. Every BIOS call
runs under a timer watchdog so a hung call aborts to a fallback instead of
freezing. It prints a readable report and holds it ~1.5 s — **pressing `V`
during that pause forces safe VGA**, the escape hatch when a card's VESA
framebuffer misbehaves. Video degrades both directions: VESA → 640×480 → VGA,
and a hung VGA set retries VESA. A VBE that reports a bogus framebuffer address
is rejected.

**Kernel init.** The IDT is installed *first*, so faults produce a panic screen
rather than a silent triple-fault reboot. Then each subsystem prints its name
**before** initialising — so a hang names its own culprit on screen. Every
timer-based wait is iteration-bounded: a dead PIT degrades the boot instead of
hanging it. `dmesg` replays the report afterwards.

**Extension load.** KERNEL services load first, in filesystem order. Shipped
APP entries come from `src/appcatalog.inc`, generated from the real source
AppDescs by `tools/appcatalog.py`. The menu exists before their code or BSS is
allocated. First launch, a registered shell command, or a file opener loads
the app and replaces its placeholder without changing its app ID. Existing
saved Notes are restored at boot. Uncatalogued third-party extensions retain
the earlier boot-loading path outside `desktop/`. Desktop extensions are read
and loaded only when explicitly requested. Apps stay resident until restart;
Task Manager can deactivate idle app modules (status 46), retaining memory
and registrations for safe reactivation. Closing a window does not unload its
extension. Reclaiming allocations or swapping private app state is not implemented.

> The boot path is **frozen**. Do not change `boot/`, the stub, or early kernel
> bring-up except for a proven bug — the interesting work all fits in a kext.

## 3. Disk and memory map

```
floppy:  LBA 0 boot | 1..255 kernel (budget) | 256 config | 257..287 slack
         | 288 superblock | 289..298 file table | 299..2879 file data

memory:  0x8000..load_end     kernel image (under 0x30000)
         0x80000..0x90000     floppy DMA (64 KB, ISA aligned)
         0x90000..0x9E000     bootstrap stack reservation, grows down
         0x100000..bss_end   kernel BSS (linker asserts <= 0x190000)
         0x270000..0x3B4000  full-floppy file-transfer buffer (1,327,104 bytes)
```

| Reservation | Under 9 MB (compact) | At least 9 MB |
|---|---|---|
| Framebuffer capacity | `0x190000..0x1E0000` | `0x400000..0x500000` |
| Shared app code/service data | `0x1E0000..0x270000` | `0x600000..0x700000` |
| Private app pages | `0x30000..0x80000` | `0x800000..0x900000` |
| Heap | `0x3B4000..RAM` (4 MB fallback if RAM size is unknown) | `0x700000..0x800000` |

`memory_layout()` selects these reservations before paging starts. Compact
mode limits the requested video mode to 640x480 so the backbuffer fits. Its
pool and arena share the low page table with the null trap; paging edits that
table in place, preserving both the null trap and code write protection.
Private pool pages are removed from the identity map and mapped only through
the owning app's virtual window. The full transfer-buffer size is retained:
`fs_read` truncates to its capacity, so reducing it would corrupt file copies.

Allocators are separate: extension addresses stay stable after relocation,
while the heap serves temporary buffers. On-demand loads use a temporary heap
buffer for the executable, preserving a file opener's existing `iobuf` data.
An allocation failure restores arena/pool cursors and address-space selection.
An entry-point failure quarantines its code until restart rather than reusing
memory that may already have published pointers.

## 4. Kernel extensions

Two kinds, declared in each `.kx`'s header:

- **`KEXT_KIND_KERNEL`** — loads early, may use port I/O, may register a service
  (`fat`, `net`, `dialogs`, `gdi`, `g3d`, `desktop`).
- **`KEXT_KIND_APP`** — registers an `AppDesc` (title, draw/key/mouse handlers,
  client size) and appears in the start menu.

A `.kx` is a **freestanding ELF32 relocatable object**. The loader relocates it
into the arena and calls `kext_entry(const Kapi *)`. The critical rule:

> **The loader resolves no kernel symbols.** An extension may only call the
> kernel through the `Kapi` table it is handed. Any undefined symbol is a load
> failure (`E41`) at boot.

Load errors surface on the boot screen as `E40`–`E44`. The full trap list —
including the compiler-emitted `memcpy`/`__udivdi3`/`__muldi3` calls that bite
newcomers — is in [MAINTAINING.md §4](MAINTAINING.md#4-writing-an-extension-kx).

## 5. Services and ABI

Three versioned interfaces, all **append-only**:

- **`Kapi`** (`KAPI_VERSION`, currently 32) — the kernel↔extension table.
- **Service ops tables** — `GdiOps`, `G3dOps`, `DialogOps`, `FatOps`, `NetOps`.
  Each carries its own `abi` field; consumers bind and gate in one call
  (`gdi_bind(k, 11)`) and **degrade** if the service is missing or too old.
- **`AppDesc`** — app registration.

KAPI v31 adds `MI_KERNEL_BYTES`, `MI_IO_BASE`, and `MI_IO_END` to
`mem_info`. Existing selector values and table offsets stay unchanged; the
version check prevents the new Memory Map from using these selectors on an
older kernel that cannot report them.

A service is registered with `register_service("name", &ops)` and found with
`service_get("name")`. This is what makes the graphics engine swappable: `gdi.kx`
can be replaced on a floppy without touching the kernel.

## 6. Window manager and compositor

- Windows live in a fixed table with a z-order array; each carries an app type +
  instance index, so several editors or file managers coexist.
- `gui_compose()` redraws **everything** into `BACKBUF` — wallpaper, desktop
  icons, every window's `draw()`, taskbar, cursor — then `flip()` blits to the
  framebuffer. There is no compositor damage tracking. On banked/VGA video,
  `flip()` compares each row against a RAM mirror and transfers only the span
  from the first changed pixel to the last. This avoids sending entire frames
  over an ISA video bus for pointer or clock changes. Linear video still uses
  sequential full-frame copies. The mirror is allocated after heap startup;
  insufficient free memory falls back to full copies.
- Consequence, and it is the main performance rule in this codebase: per-frame
  cost in *any* visible app's `draw()` is paid ~30×/second whenever anything
  animates. A full-screen dithered gradient re-quantised per frame once consumed
  the entire frame budget on a Pentium II.
- The WM narrows the clip rect to a window's client area before calling its
  `draw()`, so an app cannot scribble on the desktop — **unless** it takes the
  `surface_lock` fast path, which is honour-system and must self-clip.
- Key, pointer, and wheel callbacks run in order on the app worker. Waiting
  is timed per handler: a single operation lasting at least 250 ms shows a
  cycling Working indicator; returning clears it immediately. A continuous
  stream of short strokes, keystrokes, or scroll events never accumulates a
  false wait. Apps with stable drawing storage can opt into AppDesc.live_draw
  (Terminal and Paint do); other long-running handlers retain the placeholder.
  Device pumps leave keyboard scancodes queued while inspecting Esc for
  cancellation, and forward wheel input through the worker queue.
- **Input overlays** (`set_overlay`) are how modal UI works: popup menus and all
  the standard dialogs. An overlay captures every button event; since KAPI v9 it
  may also register a key handler (`set_overlay_key`), which is what lets the
  save dialog have a typed filename field.
- **Animation** is `timer_add` (periodic callback) or `anim_claim`, plus
  `gui_dirty()`. An animated app must remember the id `register_app` returned so
  its timer can tell whether its window is still open.

## 7. Graphics

Two extensions, layered:

**`gdi.kx`** — a GDI-class 2D vector engine over an 8bpp indexed surface:
an inverse-palette LUT (32 KB) for fast RGB→index, ordered dithering, shade
ramps, Y-X banded regions with boolean combines, polygon scan-fill (even-odd and
nonzero winding), Bézier curves, stroking with round joins/caps, antialiased
fills via supersampled coverage, and Bresenham lines.

**`g3d.kx`** — a fixed-function 3D pipeline "in the Direct3D-5 / OpenGL-1.1
mould": matrix stacks → transform → Sutherland–Hodgman clipping **in clip space
before the divide** → perspective divide → viewport → backface cull → raster.
It supports a 16-bit Z-buffer, Gouraud shading through a light-level colormap,
directional lighting, and strips/fans. It is a **client of `gdi`** — every pixel
it draws goes through `fill_poly`/`draw_line`, so 3D obeys the same clipping
contract as 2D. CPU only; no GPU is involved anywhere.

Both are software, integer/fixed-point in the inner loops, x87 only at setup
rate. Palette indices 0–47 are system colours; `gdi` claims 48–239 for shade
ramps on first use.

### Graphics fault fallback

Guarded P14 page faults, P13 protection faults and P6 invalid instructions
attributed to a registered graphics service revoke it for the rest of the boot.
GDI failure also disables G3D, which depends on it; G3D failure leaves GDI
available. The top banner reports "P14 gdi.kx - Attempting a fallback." (with
the actual fault/service). KextInfo.status is 45 for a disabled service.

This is a logical unload: the registry stops returning the service, while its
code, data and outstanding allocations remain reserved until restart. They
cannot safely be freed while older clients might retain raw pointers. Bundled
apps, desktop rendering and shared UI chrome rebind before each draw, so an
existing window can use its fallback on the next frame. The failed draw is
abandoned; input and open handlers with partial effects are not replayed.
Repeated faults and unrelated app faults keep the normal close/fault behavior.
Unguarded kernel faults still stop the OS; arbitrary memory corruption cannot
be repaired safely. Panic diagnostics show instruction and memory addresses,
code owner, active context and the decoded page-fault access.

## 8. Storage

- **Floppy** — [FLOPFS](FLOPFS.md), custom and flat. The OS's private disk.
- **USB** — FAT12/16/32 via `fat.kx` over a UHCI driver, handling both
  MBR-partitioned sticks and raw "superfloppy" volumes. The interchange medium.
  [`tools/usb.py`](../tools/usb.py) edits a stick image from the host.

Paths are addressed by **drive spec**: `a:NAME` for the floppy, `u:/PATH` for
USB. `sh_spec_split`/`sh_spec_make` in [`src/shpath.h`](../src/shpath.h) are the
one shared parse — the file dialogs hand these specs to apps.

## 9. Testing

Tests run **inside the OS on real boot** and report over serial; there is no
host-side unit rig, because the interesting code runs against the real kernel.
`kexts/selftest.c` is a kernel-kind extension that loads last (so every service
is up) and can therefore test the *live* system — it binds the real `gdi`/`g3d`
services, draws, and reads pixels back out of the framebuffer.

```
powershell -File tools/runtests.ps1
```

Pure logic lives in shared `.inc` files text-included by **both** the shipping
extension and the test, so the tested code and the shipped code are literally
the same source. See [MAINTAINING.md §3 and §5](MAINTAINING.md#3-testing-the-in-guest-harness).

## 10. Where to look

| you want | file |
|---|---|
| Boot chain | `boot/boot.asm`, `boot/stub.asm` |
| Kernel entry, init report | `src/kernel.c` |
| Window manager, compositor, screensaver | `src/gui.c` |
| Graphics primitives, palette, framebuffer | `src/gfx.c` |
| Shell and terminal | `src/apps.c` |
| FLOPFS | `src/fs.c` |
| Extension loader + the Kapi table | `src/kext.c` |
| The ABI itself | `src/kapi.h` |
| 2D engine / 3D engine | `kexts/gdi.c` / `kexts/g3d.c` |
| Shared pure cores (tested) | `kexts/*.inc`, `src/shpath.h` |
