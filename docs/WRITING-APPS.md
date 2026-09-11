# Writing FLOPNIX apps and extensions

Everything in FLOPNIX above the kernel is an extension — the file manager, the
games, the graphics engine, the FAT driver. They are all `.kx` files in `sys/`,
all loaded the same way, and none of them is special. If you can write one you
can replace any of them.

This is the practical guide: build a working app, then the reference for
everything an app can do. For the loader's internals see
[MAINTAINING.md §4](MAINTAINING.md#4-writing-an-extension-kx); for how the
system fits together see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. The shortest app that works

Save this as `kexts/hi.c`:

```c
#include "kapi.h"

static const Kapi *api;

static void hi_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w; (void)cw; (void)ch;
    api->draw_text(cx + 12, cy + 12, "Hello from a kext.", C_BLACK);
}

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Hi"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;   /* older kernel: refuse */
    api = k;
    static const AppDesc d = {
        .title = "Hi", .max_inst = 1, .in_menu = 1, .draw = hi_draw,
    };
    return k->register_app(&d) < 0;
}
```

Add `hi` to the extension list in `build.sh`, run `./build.sh`, and it is in
the start menu. That is the whole cycle.

Three things are load-bearing:

- **`kext_header`** must exist with that exact name. The loader reads it out of
  the file before relocating anything, so it can report the name and reject a
  kext built against a newer KAPI.
- **`kext_entry`** is your `main`. Return non-zero and the loader unloads you
  and reports E44.
- **`api`** is your only way to reach the kernel. Stash it and use it for
  everything. See §6 for why calling anything else fails to load.

`AppDesc` is `static const` deliberately — it must stay readable after your
entry returns (§7 explains what happens if it isn't).

---

## 2. What an app can implement

`AppDesc` is a table of optional callbacks. Fill in what you need:

| field | when it runs |
|---|---|
| `open(inst)` | a window of yours opened — reset per-instance state here |
| `draw(w, cx, cy, cw, ch)` | every frame; `cx,cy` is your client origin |
| `key(inst, k)` | a key, when one of your windows has focus |
| `mouse(inst, lx, ly, ev, cw, ch)` | clicks/drags, coords **local** to your client area |
| `wheel(inst, dz)` | mouse wheel; `dz > 0` is up |
| `drop(inst, lx, ly, type, data)` | something was dragged onto you |
| `client_size(inst, *w, *h)` | your preferred size at open |
| `min_client(*w, *h)` | smallest you can be dragged to |

Plus `title`, `max_inst` (how many windows at once), `in_menu`, `resizable`,
and `category` (`APP_CAT_GAMES`, `APP_CAT_DEV`, …) for menu placement.

`inst` is which of your windows this is, `0..max_inst-1`. Keep per-window state
in an array indexed by it — every game in the tree does this.

### Drawing

Draw only inside your client rect; the compositor clips you to it, but drawing
outside is wasted work. Coordinates handed to `draw` are absolute screen
coordinates of your client origin, so everything is `cx + x`, `cy + y`.

You get the basics free through `api`: `fill_rect`, `draw_text`,
`draw_text_clip`, `hline`, `vline`, `rect`, `panel`, `bevel`, `draw_char`,
`draw_sbar`. For gradients, dithering, polygons, lines and 3D, bind the
graphics services (§4).

There is **no damage tracking** — the whole screen recomposes every frame at
~50 Hz. That is fine as long as your draw is cheap. If your app has an
expensive layout, cache it and rebuild only when something changes; don't
recompute per frame.

---

## 3. Shell commands, timers, openers

Beyond a window, an app can register:

```c
k->register_cmd("hello", "hello - say hi", cmd_hello);   /* a shell command */
k->timer_add(50, tick, 0);                               /* every 50 ticks */
k->register_opener("txt", my_opener);                    /* handle .txt files */
```

Ticks are 100 Hz, so `timer_add(50, …)` is twice a second, and `timer_add(2, …)`
is the 50 Hz most games use. `register_opener("", fn)` makes you the *fallback*
opener for any file nothing else claims — that is how the editor opens
arbitrary files.

Two rules that are easy to get wrong:

- **A timer keeps firing after your window closes.** It is registered once and
  lives until `timer_del`. If your timer does real work, gate it on having a
  window open:

  ```c
  static int is_open(void)
  {
      int n = api->win_max();
      for (int i = 0; i < n; i++) {
          const Win *w = api->win_slot(i);
          if (w && w->used && w->type == my_type) return 1;
      }
      return 0;
  }
  ```

  The Serial Monitor once polled its UART at 100 Hz forever because it skipped
  this.

- **Never do disk I/O from `draw`.** See §7 — this one has destroyed a file.

---

## 4. Using the graphics services

`gdi` and `g3d` are extensions like yours, reached through a bound ops table:

```c
#include "gdi.h"
static const GdiOps *gfx;
...
gfx = gdi_bind(k, 11);           /* 11 = the ABI level you need */
if (gfx) {
    gfx->set_dither(1);
    gfx->fill_gradient(cx, cy, cw, 32, GRGB(214, 218, 228),
                       GRGB(180, 186, 202), 1);
    gfx->set_dither(0);
}
```

`gdi_bind` returns null if the service is missing or too old, so **always check
it** and fall back to plain fills. Every app in the tree degrades this way; it
is what lets someone strip `gdi.kx` off the disk and still have a working
system.

The `abi` number is a floor, not an exact match — ask for the lowest level that
has what you use.

---

## 5. Files

```c
int n = api->fs_read("notes.txt", buf, sizeof buf);      /* A: floppy */
api->fs_write("notes.txt", buf, n);
api->fat_read("/docs/notes.txt", buf, sizeof buf);       /* USB */
```

A: is [FLOPFS](FLOPFS.md) — flat, contiguous, 23-character names, no real
directories (`sys/gdi.kx` is one file whose *name* contains a slash). USB is
FAT via `fat.kx`, which has actual directories. `fat_*` calls return -1 when no
stick is mounted, so check.

Two FLOPFS specifics worth knowing before you write a file:

- `fs_write` returns **-2** when no single contiguous run is free, even when
  `fs_free_kb()` says there is plenty of space. Fragmentation is fatal here, not
  slow. Report it rather than retrying.
- Names longer than 23 characters are **refused**, not truncated.

Files and Desktop share transfer helpers in `kexts/fileops.inc`. Their
clipboard payload is a null-terminated, newline-separated list of drive
specifications (`a:desktop/note.txt`, `u:/docs/note.txt`); folder specifications
end in `/`. Type `file` means Copy, and `file.cut` means Cut. A consumer must
delete a Cut source only after the destination write succeeds. On partial
failure, keep the failed specifications for retry, and only update the
clipboard if it still contains the original selection. Publish `file.changed`
after filesystem changes so other open views refresh. This is an application
protocol using the existing clipboard API; it does not change the kernel ABI.

The window manager times each individual input handler. After 250 ms it
shows a Working indicator, which clears as soon as the handler returns.
Repeated short callbacks count as progress. Set AppDesc.live_draw to 1 only
if draw can safely inspect your state at every intermediate point in a
handler; Paint uses a permanently allocated, bounded canvas for this reason.
The title indicator still appears for a long operation in a live-drawing app.
Wheel callbacks use the same worker queue and fault guard as key/mouse input.

For long operations, show the busy overlay so the user knows the machine is
working rather than hung:

```c
api->busy_set("Copying", name, done * 256 / total);   /* frac<0 = no bar */
...
api->busy_end();
```

It is drawn by the compositor, so it survives the redraws that happen while
your I/O blocks. Drawing your own progress box and calling `flip()` does not
work — see §7.

---

## 6. Why your extension won't load

The loader resolves **no kernel symbols at all**. Everything goes through
`api`. An undefined symbol is E41 at boot, and the trap is that **clang emits
calls you never wrote**: struct assignment becomes `memcpy`, zeroing becomes
`memset`, 64-bit division becomes `__udivdi3`.

So: define your own `memcpy`/`memset` if you do bulk memory work (copy the ones
at the top of `kexts/gdi.c`), and avoid 64-bit `/` and `*` entirely. `build.sh`
runs `llvm-nm --undefined-only` on every extension and fails the build if
anything is unresolved, so you will normally catch this before booting.

The full error list:

| code | meaning |
|---|---|
| E40 | not a loadable `.kx` — bad ELF, truncated, or no `kext_header` |
| E41 | unresolved symbol (see above) |
| E42 | out of arena or private-block space |
| E43 | built against a newer KAPI than this kernel |
| E44 | your `kext_entry` returned non-zero |

The KEXT Inspector (Development menu) shows every extension, its kind, load
address, size and status — start there when something is missing.

---

## 7. The four rules that have actually broken things

These are not hypothetical; each one cost real debugging.

**Never call disk I/O from a draw callback.** `fs_*`/`fat_*` block, and while
they wait they pump the GUI to keep it alive. Pumping recomposes, recompose
calls your draw, and your draw calls the disk again — the nested call is
refused by the device guard. A refused *write* used to delete the file it was
updating. Do I/O from a click, a key, or a timer; from `draw`, only set a flag.

**Registrations are owned by whoever is running.** A timer or command you
register is later invoked by the kernel, which makes your extension resident
first. This works because ownership is recorded when you register. It is
handled for you now, but it is why you must not hand out callbacks from odd
contexts and expect your globals to be there.

**Your `AppDesc` and ops tables must be `static const`.** They are read long
after your entry returns, from contexts where only shared read-only memory is
mapped. A non-const one lives in your private data and will read as garbage.

**Check every bind and every service call for null.** `gdi_bind`, `fat_*`,
`usb_*` — all can be absent. The system is designed to run with pieces missing.

---

## 8. Testing what you write

Pure logic goes in a `.inc` file and gets unit-tested; the harness runs in the
guest and prints to serial:

```
powershell -File tools/runtests.ps1 -Cpu pentium2
```

Put the arithmetic — parsing, layout maths, state machines — in
`src/yourthing.inc` as `static inline` functions, include it from both your
kext and `kexts/selftest.c`, and assert it there. Roughly two thousand
assertions run this way, and they have caught far more than they cost. The
pattern is described in [MAINTAINING.md §5](MAINTAINING.md#5-shared-pure-cores-the-inc-pattern).

Then actually drive the app. Build, boot it in QEMU, and use the thing —
open it, use it, close and reopen it, run it alongside another app. Features
that looked correct have failed the moment they met a real user path.

---

## 9. Reference

- `src/kapi.h` — the whole API in one file, commented. When in doubt, read it;
  it is the contract.
- `kexts/hello.c` — the smallest real example.
- `kexts/clock.c`, `kexts/calc.c` — small, complete apps.
- `kexts/pong.c` — timers, held-key input, per-instance state.
- `kexts/files.c` — the large end: lists, selection, dialogs, drag and drop.
- `kexts/gdi.c` — a KERNEL-kind extension that publishes a service.
