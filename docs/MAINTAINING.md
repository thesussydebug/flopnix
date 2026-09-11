# Maintaining FLOPNIX

How to build, test, extend and not break this thing. [README.md](../README.md) is
the user-facing tour; [ARCHITECTURE.md](ARCHITECTURE.md) explains how the system
works and [FLOPFS.md](FLOPFS.md) specifies the disk format. This file is for
whoever is editing the source.

Most of what follows is not obvious from reading the code, and several of the
rules fail **at boot on the floppy** rather than at compile time. That is the
reason this document exists.

---

## 1. Toolchain

MSYS2 with:

| package | used for |
|---|---|
| `nasm` | `boot/boot.asm`, `boot/stub.asm` |
| `mingw-w64-clang-x86_64-clang` | the C compiler (cross-compiles to i386) |
| `mingw-w64-clang-x86_64-lld` | `ld.lld` links the kernel |
| (bundled with clang) | `llvm-objcopy`, `llvm-nm` |
| `python` | image tooling (`tools/fscp.py`, the checksum stamp) |

Everything is built **freestanding for bare metal**, never for Windows:

    --target=i386-unknown-none-elf -ffreestanding -fno-builtin
    -fno-stack-protector -fno-pic -fno-asynchronous-unwind-tables
    -mno-sse -mno-mmx -O2 -Wall -Wextra -Isrc

`-mno-sse -mno-mmx` matter: this targets a Pentium II. The x87 FPU *is*
available (the 3D engine uses it), but SSE is not.

## 2. Building

    bash build.sh          # from Git Bash or MSYS2

Produces `built/flopnix.img` (exactly 1,474,560 bytes), `built/flopnix.ku`
(kernel update), and `built/kexts/*.kx` (production extensions). Compiler
intermediates stay in `out/`; finished files are not copied into the root.

What it does, in order:

1. **Documentation version gate** - the README title version must equal `OS_VER` in
   `src/os.h`, or the build stops. Docs that silently rot are worse than none.
1. **Usage gate** - every command `shell_exec` dispatches must have a
   `sh_usage()` line in `src/shcmd.inc`. Without one the shell answers a
   mistyped invocation with "command not found" about a command that plainly
   exists, which is what that table is for.
2. Assembles `boot/boot.asm` (boot sector) and `boot/stub.asm` (real-mode setup).
3. Compiles each kernel `.c` in `src/`.
4. Compiles each extension in `kexts/` to a `.kx`, and runs the **E41 gate**
   (see §4) on every one. A single unresolved symbol fails the build.
5. Links the kernel, flattens it to a binary, and **stamps a checksum + sector
   count** into the stub header. The stub verifies this at boot as error `E09`,
   which catches a worn floppy loading garbage. The stamping loop in `build.sh`
   must byte-match the 16-bit rotate-add loop in `boot/stub.asm` - if you touch
   one, touch both.
6. Writes the image, then installs every `.kx` into the image's FLOPFS under
   `sys/` using the same `tools/fscp.py` a user would run.

If the kernel grows past 255 sectors the build stops: it would overlap the
config sector at LBA 256.

## 3. Testing: the in-guest harness

Shared logic has native tests in `tools/appcoretests.c`, `tools/fileopstests.c`,
and `tools/fatstabilitytests.c`. The full integration suite runs inside the
guest OS and reports over the serial port.

    powershell -File tools/runtests.ps1        # ~20 s

This builds `kexts/selftest.c`, drops it onto a **copy** of the image, boots
QEMU headless with COM1 captured to a file, and prints:

    ok   spec_split
    not ok g3_clip
      FAIL: NEAR(b.w, G3_WEPS, 4)
    SELFTEST DONE: 345 pass 0 fail

`selftest.kx` is a `KEXT_KIND_KERNEL` extension and is **not** in the shipped
image - only the test copy has it. It loads last, so by the time it runs every
service is registered and it can test the *live* system (it binds the real
`gdi`/`g3d` services, draws, and reads pixels back out of the framebuffer).

### Adding a test

1. Write the failing check first. Put pure logic in a shared `.inc`/`.h` (§5) so
   the test and the shipping code compile the *same source*.
2. Add `static void t_thing(void) { CHECK(...); }` and one `run("thing", t_thing);`.
3. Run it and **watch it fail** — with the stub still in place. A test that has
   never failed has not proven it tests anything.
4. Implement, re-run, watch it pass.

`RESULT: INCOMPLETE (no DONE line)` means the kext did not load or the guest
hung — usually an E41 (§4) or a crash before the suite finished, not a failed
assertion.

### Networking integration checks

After building, compile the test-only network probe with
`bash tools/mkkext.sh kexts/nettest.c`, then run
`python tools/nettests.py --nic ne2k_pci` (also `ne2k_isa`, `rtl8139`, `tulip` and `pcnet`).
This boots image copies on both QEMU's default DHCP subnet and a private
192.168.76.0/24 subnet, checks the live address/mask/gateway/DNS, reacquires
the lease through NetOps, pings the gateway and checks captured DHCP packets.
The probe is not shipped. Logs and packet captures stay in `out/nettests/`;
the runner only stops the QEMU processes it creates.
Add `--lifecycle` to exercise dropped discovery, short-lease renewal,
rebinding, expiry, NAK and recovery against an isolated local Ethernet peer.
Use `--offline` to verify that an unanswered boot retries discovery and leaves
all address settings unconfigured rather than inventing a QEMU static address.

`kexts/dhcp_core.inc` contains the tested reply parser, request builder,
server/state matching and lease-phase arithmetic. `net.c` performs the NIC
I/O and tracks retry/lease time through `net_poll`, with no blocking work in
the background DHCP pump. Initial acquisition has a 12-second foreground
budget; unanswered requests keep retrying afterwards. Static mode disables
DHCP when `NET_DHCP_OK` is cleared by the configuration caller.

## 4. Writing an extension (.kx)

> Writing an app rather than maintaining the loader? Start with
> [WRITING-APPS.md](WRITING-APPS.md) — it has a working example, every
> callback, and the rules that bite. This section is the loader's side.

An extension is a **freestanding ELF32 REL object**, loaded by `src/kext.c` into
an arena and relocated. Two kinds:

- `KEXT_KIND_KERNEL` - loads early, may use port I/O, may register services
  (`fat`, `net`, `dialogs`, `gdi`, `g3d`).
- `KEXT_KIND_APP` - a program; registers an `AppDesc` and appears in the menu.

Load failures show on the boot screen:

| code | meaning |
|---|---|
| E40 | not a loadable `.kx` (bad ELF / truncated / no header) |
| **E41** | **unresolved symbol** — see below |
| E42 | extension arena out of memory |
| E43 | built against a newer KAPI than this kernel |
| E44 | `kext_entry` returned failure |

### The E41 trap (read this one)

**The loader resolves NO kernel symbols.** An extension may only call the kernel
through the `Kapi` function-pointer table it is handed. Any undefined symbol in
the object is E41 at boot.

The trap: **clang emits calls you did not write.** Even under `-fno-builtin`:

- struct assignment / array loops → `memcpy`, `memset`, `memmove`, `memcmp`
- 64-bit division → `__udivdi3` / `__divdi3`
- 64-bit multiply → `__muldi3`

So a graphics kext must **define its own** `memcpy`/`memset`/`memmove`/`memcmp`
(see the top of `kexts/gdi.c`), and must avoid 64-bit `/` and `*` entirely —
use inline asm or restructure the maths to stay in 32 bits. The polygon
rasteriser sorts edge crossings with an i32 cross-multiply *specifically* to
avoid `__muldi3`.

`build.sh` runs `llvm-nm --undefined-only` on every extension and fails the
build if anything is unresolved, so this is caught at build time now. Do not
remove that gate.

## 5. Shared pure cores: the `.inc` pattern

Pure, testable logic lives in a header-like `.inc` that is **text-included by
both** the shipping kext and `selftest.c`:

    kexts/gdi_rgn.inc      region algebra        kexts/gdi_poly.inc   polygon scan
    kexts/gdi_curve.inc    Bezier eval/flatten   kexts/gdi_stroke.inc stroking
    kexts/gdi_aa.inc       coverage + blend      kexts/gdi_line.inc   Bresenham
    kexts/g3d_math.inc     matrices, clip, trig  src/shpath.h         path/spec parsing
    src/shcmd.inc          shell command table   src/vbank.inc        banked-VESA banking
    kexts/net_wire.inc     DNS/DHCP/HTTP/URL     kexts/net_tcp.inc    TCP state machine

Rules: no heap, no `Kapi`, operate on caller-provided buffers, and mark
functions `static inline` if some translation unit may not use them (otherwise
`-Wunused-function` fails the build). Where a walk needs to write pixels, take a
**plot callback** (`gdi_line.inc`) so the unit-tested code and the shipped code
are literally the same function.

## 6. ABI rules

Three versioned interfaces. All are **append-only**: add at the END, never
reorder or change an existing signature.

- **`Kapi`** (`src/kapi.h`, `KAPI_VERSION`) - the kernel↔extension table. Bump
  the version when you append. Every kext checks
  `if (k->version < KAPI_VERSION) return 1;`, and the loader rejects a kext
  built against a *newer* KAPI with E43.
- **Service ops tables** (`GdiOps`, `G3dOps`, `DialogOps`) - each carries its own
  `abi` field. Consumers bind and gate in one call:

      g = gdi_bind(k, 11);        /* 0 if missing or too old */
      if (g) { ...use it... }     /* else: degrade, never hard-fail */

  **Always degrade.** An image can legitimately ship without `gdi.kx`.
- **`AppDesc`** - the app registration struct. Since v10 an app may claim its
  start-menu category with `.category = APP_CAT_PROGRAMS / _GAMES / _SYSTEM /
  _DEV`; leaving it 0 (`APP_CAT_AUTO`) keeps the kernel's title-based
  classification, so old kexts need no change.
- **`NetOps` has no `abi` field** (it predates the convention), so its v11 tail
  is gated on a magic marker instead: `services.c` calls `dns`/`http_get` only
  when `nops->v11 == NET_ABI_V11`. A pre-v11 `net.kx` simply ends before those
  members, and whatever garbage follows will not match the marker. Use the
  same trick for any other table that shipped without a version field - never
  just call off the end and hope.

Kexts load in **FLOPFS name order**, which is alphabetical — so `g3d.kx` loads
*before* `gdi.kx`. A service that consumes another service must **bind lazily**
(on first use, not in `kext_entry`). See `gdi()` in `kexts/g3d.c`.

## 7. Things that will bite you

- **The boot process is frozen.** `boot/boot.asm`, `boot/stub.asm` and the early
  kernel bring-up are not to be changed except for a proven bug. Everything
  interesting can be done in a kext.
- **Palette indices 0..47 are system colours**; 48..255 are free. `gdi.kx`'s
  shade ramps claim 48..239 on first use. Overwriting 0..47 restyles the whole
  GUI.
- **`surface_lock` is honour-system.** Once a kext holds the raw framebuffer
  pointer the WM can no longer clip it — every op must self-clip to
  `clip_rect_get`. Getting this wrong scribbles on the desktop.
- **The compositor redraws everything**, every frame, on the clock. There is no
  damage tracking and presentation is no longer gated on `gui_dirty` — that
  gating is what made the whole UI drop to 2 FPS whenever the mouse stopped
  moving, because the only thing left marking the screen dirty was the
  half-second cursor blink. So per-frame cost in *any* visible app's `draw()`
  is paid ~50x/sec. Re-quantizing a full-screen dithered gradient every frame
  once ate the entire frame budget on a Pentium II.
- **Never call blocking disk I/O from a `draw` callback.** `fs_*`/`fat_*` pump
  the GUI while they wait; the pump recomposes; recompose calls `draw`; the
  nested call is refused by the device guard. A refused *write* used to clear
  the directory entry, which deleted the file being updated. Set a flag in
  `draw` and do the I/O from a timer or an input handler. `fs_write` now
  restores the old entry on any failure, but the re-entrancy is still yours to
  avoid.
- **Ctrl+letter arrives as a control code**, not as the letter: `handle_sc`
  sends Ctrl+A as `1`, Ctrl+C as `3`. Comparing the key to `'c'` while Ctrl is
  held never matches — the file manager's Ctrl+C/X/V/A were dead for exactly
  this reason. Fold with `kb_unctrl(k, ctrl)` from `src/textfield.inc`, into a
  *local*: Enter, Tab and Backspace are control codes too (10, 9, 8), so
  folding over `k` turns Enter into `'j'`.
- **A registration outside `kext_entry` belongs to whoever is running.**
  Ownership decides which private memory window is mapped when the callback
  later fires. Seven apps arm their timer from `.open`, i.e. after load time;
  before `ks_owner` those timers recorded "no owner" and ran with another app's
  memory mapped, silently corrupting it. Use `kext_owner_now()` for any new
  registration table.
- **Animation** = `timer_add` (a periodic callback) or `anim_claim`, plus
  `gui_dirty()`. An app that animates must capture the id `register_app`
  returns; a timer callback that cannot identify its own window will spin
  forever or never fire.
- **`tools/*` and scratch drivers that read kernel variables by address will
  break silently** whenever the kernel's BSS layout shifts. The QEMU UI driver
  resolves `mx` from `out/kernel.elf` with `llvm-nm` for exactly this reason. If
  UI automation suddenly "does nothing", suspect a stale address before
  suspecting the OS.
- **Banked video costs more than linear.** On a VBE 1.2 card with no linear
  framebuffer the stub sets a *banked* mode (`BootInfo.vbe == 2`) and `flip()`
  walks the 64 KB window with chipset bank registers (`src/vbank.inc`). The
  bank scheme is picked by PCI vendor id, then by ISA presence probe; if the
  card is not one of the four known families the kernel programs VGA 13h by
  hand and runs 320x200 rather than display two-thirds garbage. Test it with
  `STUBDEF=-DFORCE_BANKED bash build.sh` plus `qemu -vga cirrus`.
- **Never pump from inside the clock path.** `gui_pump` calls `gui_tick`,
  which calls `update_clock` -> `rtc_read`. Adding a pump to `rtc_read` (or
  anything `gui_tick` reaches) is circular; `gui_pump`'s `inside` flag stops
  the recursion, but the right answer is not to pump there at all.
- **Pump throttling.** A wait that normally succeeds immediately (the FDC FIFO
  waits in `wait_write`/`wait_read`) must not pump every iteration - that
  would tax every byte of every transfer. Pump every Nth spin instead, so the
  repaint only happens once the device has actually stalled.
- **A blocking kernel op freezes the whole GUI unless it pumps.** The shell
  runs inside the main loop, so while a command blocks (a network wait can be
  seconds), `gui_tick`/`flip` never run and the desktop looks hung. The fix is
  `gui_pump()` (KAPI): a blocking loop calls it each iteration to advance the
  clock, step animations and track the cursor - but it deliberately does NOT
  dispatch clicks or keystrokes, so it cannot re-enter the very command that is
  blocking. It returns 1 on Esc so the wait can be cancelled. `net.c`'s
  `net_wait()` is the model: `net_poll(); if (gui_pump()) net_cancel = 1;`. Any
  new long-blocking kernel service should do the same rather than bare-spin.
- **RTL8139 buffers are DMA targets.** `rtl_rx`/`rtl_txb` are handed to the
  chip as *physical* addresses, and DMA does not go through the MMU. Paging is
  on, but the map is IDENTITY, so a static array's address is still its
  physical address and these keep working. That is one of the reasons the map
  is identity (see the header of `src/paging.c`) - the day any of it stops
  being identity, every NIC and floppy DMA buffer becomes a bug.
- **The guest test runner uses an image copy.** It stops only the QEMU process it starts and returns an error for failed or incomplete suites.
- **`IOBUF_SZ` is shared scratch.** Several apps use `api->iobuf` for file I/O;
  do not hold it across a call that might also use it.
- **Never preempt above the EOI.** `isr_dispatch` calls `thr_tick()` at the
  very bottom, after `outb(0x20,0x20)` and after `in_irq--`. A switch taken
  before the EOI abandons the handler without acknowledging the PIC, so IRQ0
  stays masked until that one thread is scheduled again. If it is a CPU-bound
  app handler, it never is: the timer dies, every tick-driven loop spins
  forever, and the machine hangs at 100% CPU with a frozen clock. Do not move
  that call back up into the `switch`.
- **The private window belongs to the thread context.** There is one mapping at
  `KEXT_PRIV_VA` and every extension shares it, so `reschedule()` saves
  `kext_current()` before `thr_switch` and re-enters it on resume. Without that
  a preempted app resumes to find another app's block mapped at its own
  address - and because that address is still *mapped*, nothing faults and
  `kext_fix_window` never gets a chance. It silently reads and writes the wrong
  extension's memory. The demand-fix only catches *unmapped* accesses.
- **Apps should not draw outside their draw callback.** The compositor runs on
  the main thread and app handlers now run on `appinput`, so drawing from a
  key/mouse handler races the frame. `app_busy()` covers the window being
  handled, not the rest of the screen. Benchmark hands each metric to a
  main-loop timer, measures the actual clipped raster area, and saves/restores
  that area. The shell pumps only between metrics, before scheduling the next
  callback, so progress reaches the hang watchdog without concurrent drawing.
  Do not move its raster work into an input callback.
- **Do not reuse USB transfer records before the queue is retired.** UHCI
  completion requires both inactive descriptors and a terminal queue-head
  element. The controller can write a descriptor's status before its queue
  pointer; starting another transfer between those writes loses the new
  queue. `run_tds` waits for both, under the existing timeout. QEMU shows this
  ordering in [`uhci_process_frame`](https://github.com/qemu/qemu/blob/master/hw/usb/hcd-uhci.c).
- **Long transfers are progress, not hangs.** Successful floppy sectors and
  USB read/write chunks call `app_note_io`. Only I/O completed by the current
  app input worker advances `app_io_tick`; the flight recorder cannot keep a
  stuck app alive. The watchdog extends its hard deadline on these completions.
  Pumping alone still has a one-minute ceiling, and stopping all progress still
  triggers the ordinary three-second check. Keep device failures out of this
  completion signal.


## 8. Hardware support

The native runner in `tools/fatstabilitytests.c` covers FAT write failures, long-name parsing, and filesystem bounds.

What the OS can actually drive, and where to add more.

**Video** (`boot/stub.asm` picks the mode, `src/gfx.c` + `src/vbank.inc` drive it)

| card | result |
|---|---|
| anything with VBE 2.0 + LFB (QEMU std/cirrus, S3 ViRGE/Trio64V2, Matrox, ATI Rage, Banshee) | linear 640x480 / 800x600 / 1024x768 |
| VBE 1.2, banked only, Cirrus GD542x+ / Tseng ET4000 / S3 / Trident 8900-9000 | same resolutions via the 64 KB window |
| VBE 1.2 on an unrecognised chipset | VGA 320x200 (the kernel sets 13h itself) |
| no VESA at all | VGA 320x200 |

To add a chipset: give `vbank_detect` its PCI vendor id, add its bank-register
sequence to `vbank_prog` (pin every byte in a `t_vbank_prog` test first), and
add an ISA probe to `vb_find_scheme` if the card predates PCI.

Banked/VGA presentation keeps an exact `SW * SH` byte RAM mirror, allocated
after heap initialization only when at least another 128 KiB remains free.
`fb_span` finds changed row intervals without reading VRAM; `vblit_span`
splits those intervals across 64 KiB bank boundaries. Both are shared with
the selftest. Linear framebuffers retain full copies; allocation failure
also safely falls back to full copies. `fb_copy_bytes` and `fb_skip_bytes`
are cumulative debugger counters for transferred and avoided bytes, including
boot; take unsigned deltas around a scenario when measuring savings.

This matters on 86Box configurations with an ISA Cirrus GD5420: raising CPU
speed does not remove ISA video-transfer costs. Test the banked path using
`STUBDEF=-DFORCE_BANKED bash build.sh` and QEMU `-vga cirrus`; rebuild normally
before shipping. QEMU can verify pixels and traffic reduction, but does not
establish an 86Box frame-rate improvement. Full-screen animation still changes
most pixels, and composition still redraws the full RAM backbuffer.

Benchmark reports the median of three 50-tick samples per metric, using
actual elapsed 100 Hz PIT ticks, including the final batch. Its integer loop
counts additions (not instructions/MIPS); its 16 KiB copy measures a warmed,
cache-sized working set; its raster test measures RAM fills, excluding VRAM.
Rates use a saturating 64-bit intermediate without compiler-runtime imports
(`bench_core.inc`). Guest clock speed and scheduling affect these measurements;
do not interpret them as host performance or isolated physical RAM bandwidth.

**Network** (`kexts/net.c`)

| NIC | probe |
|---|---|
| AMD PCnet PCI | `1022:2000`, software-style-2 DMA rings |
| RTL8139 (PCI) | `10EC:8139` / `1186:1300`, bus-master ring |
| NE2000 PCI / RTL8029 | `10EC:8029` and compatible clone IDs in net.c |
| NE2000 ISA | configured port, or 0x300 then 0x280 (PROM `57 57` signature) |
| ADMtek AN985 / AN981 (Network Everywhere NC100) | `1317:0985` / `1317:0981` |
| DEC 21143 tulip | `1011:0019` - QEMU's `-device tulip`, for testing the ring |

Stack: ARP, ICMP echo, DHCP (address, mask, router, DNS), DNS resolution, and
a single-connection TCP client used by HTTP/1.0 GET. The protocol logic lives
in the two tested `.inc` cores; `net.c` is only drivers and glue.

### The tulip family, and what emulation will not tell you

The NC100 and its DEC relatives are bus-master parts, and three of their
settings only matter on a real PCI bus. All three were wrong here for a while
because QEMU forgives every one of them. The vendor's own Linux driver ships
on the card's disk and is the authority:

- **CSR0 must not be 0.** Zero asks for an unlimited burst with no cache
  alignment. Use `0x01A08000` on i386, the reference driver's own default.
- **CSR6 is written in stages**, and the Comet/Centaur parts do **not** want
  store-and-forward: port select `0x00040000` before the reset, then `|ST`,
  then `|ST|SR`. `tul_csr6()` in `kexts/phylink.inc` pins the value.
- **The PCI latency timer is often 0 from a period BIOS**, which starves the
  card of the bus. Raise anything under 10 to 64, and set a non-zero cache
  line size, or the write-and-invalidate bit in CSR0 is not legal.

The ADMtek parts need no EEPROM dance: the MAC is at register `0xA4`/`0xA8`
and the RX filter is registers too, while the DEC parts filter through a
setup frame on the TX ring (skip it and only broadcast arrives, so DHCP works
and every unicast reply is dropped). Their MII registers 0-6 are mapped at
`0xB4 + reg*4`, which is where link state comes from.

**When it does not work, run `netdiag` first.** It prints the NIC and its I/O
base, whether the MAC was read from the card or invented, link state, CSR0/5/6,
the MII registers, ring slots and rx/tx frame counts. That distinguishes an
unplugged cable from a dead driver in one screen, which no amount of reasoning
about the code will do.

## 9. Layout

    boot/     boot sector + real-mode stub (FROZEN)
    src/      the kernel: shell, WM, gfx, FLOPFS, USB, kext loader, KAPI
    kexts/    extensions (*.c -> sys/*.kx) + the shared pure .inc cores
    tools/    fscp.py (install a file into an image), runtests.ps1, write-floppy.ps1
    built/    copy-ready artifacts regenerated by build.sh
    out/      disposable build scratch (kernel.elf lives here - useful for nm)


## Desktop files, compact memory and deferred apps

The desktop enumerates ordinary `desktop/*` FLOPFS entries; `desktop` has a
normal directory marker. It does not persist a shortcut list. Migration reads
old `desktop.lst` entries at boot, copies the file contents (following the old
LBA identity if renamed), checks existing copies by size/CRC, and archives the
list as `desktop.old` only after success. Failed migration keeps both source
files and the list. Test migration and file operations on image copies.

`src/memlayout.inc` is the authoritative physical layout. The 4 MB profile
requires the relocated DMA/stack and transfer buffer, 640x480 backbuffer cap,
and the shared-low-page-table handling in `paging.c`; lowering a label alone
is insufficient. The kernel BSS bound is checked by the linker. Test with
QEMU `-m 4` as well as the normal 32 MB profile.

`tools/appcatalog.py` runs before kernel compilation and generates metadata
from the shipped AppDescs and their command/opener registrations. It fails
on unsupported descriptor syntax instead of hiding an app. Known app paths
skip the boot ELF read; uncatalogued extensions outside `desktop/` still load
at boot. Both boot passes skip `desktop/` before reading file contents, even
for kernel-kind or malformed extensions. Desktop icons use the cached FLOPFS
directory table, so there is no second desktop database to keep synchronized.
Opening a desktop `.kx` explicitly loads it and opens its registered app.
Deferred loads retain the app ID, serialize loader activity, preserve caller mappings
and file payloads, and protect code pages again on exit. Code and private
state stay resident until restart. Do not describe this as demand paging or
claim that closing a window unloads its extension.

KAPI 32 adds `kext_unload(index)`: Task Manager's Modules view can deactivate
an idle app and load it again. Status 46 means unloaded, with its allocation
reserved until restart. Loading the same path reactivates it without rereading
the disk, rerunning entry, duplicating registrations or allocating more memory.
Commands/openers are unavailable while unloaded; an explicit app launch may
reactivate it. This does not reload edited code from disk. System-kind modules,
published services, IRQs, timers, key/shutdown/event hooks, live threads and
open windows prevent unloading. The check and transition exclude preemption
and serialize with the loader. Raw third-party pointers are why memory is
retained. Never describe this operation as reclaiming RAM.

`kexts/fileprops.inc` supplies independent Properties windows for Desktop and
Files. Register the descriptor during entry so its owner is the correct kext;
each window snapshots its metadata and outlives its source Files window.
Deferred app descriptor replacement matches the catalog title, allowing Files
to register its auxiliary Properties type without replacing its own descriptor.
The native popup accepts keyboard navigation and outside clicks reach another
menu tab in the same click. `menushade.h` binds GDI on every draw for the shared
popup/Paint gradients and retains a flat fallback when GDI is unavailable.

Compile the unshipped `desktopfixture.c` and `desktoptest.c` with mkkext.sh,
then run `python tools/desktoptests.py`. It tests boot exclusion, explicit
desktop launch, repeated deactivation/reactivation, protection checks, menu
input, picker/buttons, Properties ownership and interaction, and Paint.
Use `--scenario selftest` after compiling selftest.c, and repeat with
`--cpu qemu32` and `--memory 4`. `--no-gdi` checks the flat fallback. Only disposable images and owned QEMU processes
are used; none of these probes belongs in built/.

CPU accounting (`src/cpuaccount.inc`) follows the running scheduler thread and
nested app callback, including app timers. Waiting time is charged to whatever
runs during the wait. The remainder returned by `cpu_usage(-1)` includes both
kernel work and idle time; Task Manager labels its aggregate as **Apps CPU**.
Two windows of one app still share a per-app figure. Painting a visible window
is real work even when its contents have not changed: the compositor still
redraws visible windows when another app animates. Task Manager now removes
its refresh timer on close, and GFX Demo only runs its animation timer on the
3D page while unpaused.

### Graphics recovery checks

src/gfxfault.inc tests attribution policy and one-way dependency revocation.
kexts/gfxfaulttest.c is a test-only probe: compile with tools/mkkext.sh and
install as sys/gfxfaulttest.kx on a **copy** of the built image. It is never
shipped. In that QEMU guest, "gfxcheck gdi" corrupts the GDI polygon entry;
open GFX Demo and verify the P14 fallback banner and missing-API message.
"gfxcheck g3d" instead corrupts creation of a 3D context; select 3D animation
and verify that 2D drawing still works. "gfxcheck background" faults the
background renderer. "gfxcheck null" is an unrelated command fault and must
not disable either graphics service. "gfxcheck status" prints service
availability, extension status and fault count to the terminal and COM1.
Use a fresh image copy for each case; verify no repeated faults, existing
windows remain usable, and repeat with 4 MB RAM.

Graphics consumers must rebind before drawing, including shared UI helpers.
Do not free or reload status-45 service allocations: stale raw pointers can
exist in third-party extensions. This is quarantine until restart, not memory
reclamation. Do not retry an input operation automatically after a fault.

### Paint and waiting regression checks

The shared busycore tests cover a stream of short events, immediate completion,
window changes, and timer wrap. The paintview tests cover zoom coordinates,
viewport bounds, and odd/even image flips. Run the guest suite on both CPU
profiles and with 4 MB, then use disposable QEMU images for interaction checks:

- Hold a pencil/shape drag through several seconds and release outside the
  canvas. The canvas stays visible and the release ends the stroke.
- Zoom, pan with arrows/wheel (Shift+wheel for horizontal), draw, then save.
  The BMP dimensions remain the canvas size, independent of magnification.
- Save to desktop/, reopen, Save As a second file, flip it, undo, and resave.
  Check the original file remains unchanged and the saved pixel data matches.
- Use Crash Test's slow handler: Working appears during the operation and
  disappears after return. Terminal keeps its output visible during a pumped
  command, and text typed during that wait executes afterward in order.

### Expanded network configuration

See [NETWORKING.md](NETWORKING.md) for adapter coverage, persistent settings,
manual IPv4 and additional QEMU regression options. Hardware coverage must be
distinguished from emulation results; newly recognized clone IDs are not proof
that every card revision has been tested.

### Emergency panic and kernel-stall handling

The ordinary panic screen remains in gfx.c. emergency.c/emergency.S provide
independent terminal diagnostics when that renderer faults or exceeds its
500 timer-interrupt budget when the timer is healthy. Only the timer IRQ is
unmasked while the ordinary panic renderer runs, and its early dispatcher only acknowledges/checks the
deadline: no scheduler, device hook, input handler or normal timer work runs.
Once the first complete screen is visible, interrupts are disabled again
before the existing best-effort USB report write. The rendering deadline must
never interrupt a filesystem update; secondary exceptions still take the
emergency path.

Emergency entry abandons the interrupted stack, uses a reserved 8 KB stack,
and switches to a private identity page directory when paging is active.
It clears PGE to invalidate stale global translations. No path returns to
corrupted code. A ring-0 task gate at IDT vector 8 uses a dedicated 32-bit TSS
(GDT selector 0x30), allowing a double fault to enter even with an unusable
ordinary stack. The old TSS provides the saved instruction context; it is not
proof of the original corruption site. Early boot before GDT/TSS installation
cannot use this task gate.

Emergency output uses bounded local number formatting, direct COM1 output,
and direct 8-bit framebuffer writes. A bounded/checksummed video snapshot and
copy of the boot font are captured at graphics initialization. The renderer
supports linear, VGA and the existing bank-switch sequences; it does not call
normal drawing, allocate memory, acquire locks, look up extensions or write to
a disk. Invalid video metadata disables graphical output while serial output
continues. A build gate rejects unexpected emergency.o dependencies. Preserve
that separation when editing diagnostic code.

The main-loop watchdog arms only after boot/autoexec. The main loop and its
GUI pump reset its counter; unrelated worker activity does not reset it.
After 3000 timer interrupts without progress it enters the emergency halt.
These budgets are approximately 5 and 30 seconds at the normal PIT rate,
not a promise of wall-clock timing on degraded hardware. Elapsed-time hang
intervention is suspended while kupd_critical protects an in-place kernel
update. Actual exceptions remain fatal when recovery is unsafe.

An NMI goes directly to emergency handling, including while ordinary IRQs
are disabled. There is no automatic hardware NMI generator configured: the
PIT watchdog cannot detect an IRQ-disabled loop, broken IRQ delivery, a dead
processor or destroyed emergency code/GDT/IDT. QEMU's inject-nmi can be used
for a stopped guest. Arbitrary memory edits are still capable of destroying
all diagnostics; this is best-effort reporting, not recovery or memory safety
for deliberate kernel modification.

Run python tools/emergencytests.py after building. It installs the unshipped
emergencytest.kx only on disposable image copies and verifies terminal halt,
interrupt masking, emergency stack/page directory, serial diagnostics and
screenshots. Cases cover the normal panic, faulting/infinite-loop formatters,
a faulting draw function, a real double fault from ESP=0, a kernel loop, an
IRQ-disabled loop followed by injected NMI, a removed framebuffer mapping,
invalid video metadata, damaged normal display state, a degraded timer, and
preservation of an original page-fault address through a second exception.
Use --usb-scratch --cases normal formatter-fault to verify a normal report
and matching FAT copies on a generated FAT16 image, and no emergency disk
write. Repeat selected cases with --memory 4 --cpu qemu32 and --video vga.
For banked Cirrus, build with STUBDEF=-DFORCE_BANKED, run with
--video cirrus, then rebuild normally before delivering built/. Keep the
normal image, .ku and all production kexts synchronized. Never ship the probe.
