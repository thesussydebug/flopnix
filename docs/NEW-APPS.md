# Character Map, 2048, Breakout, Base Converter, Archive Manager and Text Web

These extensions appear in the Programs menu, except 2048 and Breakout in Games. They load
on first use. GDI supplies the familiar gradient header when available; the
apps also work with flat drawing. None installs an idle animation timer.

## Character Map (`charmap.kx`)

The 16 by 16 grid displays the OS font's 256 byte values. Click to copy one
byte, or use the arrows/Home/End and Enter (Ctrl+C also copies). The footer
shows decimal and hexadecimal values. Paste with Ctrl+V in Editor or Notes;
both now accept extended font characters. Notes also supports Ctrl+C to copy
the note. Control bytes, including zero, are copied exactly, but text editors
may filter them or treat them as line breaks or terminators.

## 2048 (`game2048.kx`)

Arrow keys or WASD slide tiles. Equal tiles merge once per move. Only a move
that changes the board adds a new tile (90% twos, 10% fours). U or Ctrl+Z undoes
one move, including the score and random state. N/New game starts over; reaching
2048 allows continued play. Best score is for this boot session. Closing and
reopening resumes the board. No disk writes or timer are needed.

The default client is a compact 272 by 332 pixels, with score and controls
above the board. GDI provides warm tile colors; palette drawing remains
available without GDI. The window can still be enlarged.

## Breakout (`breakout.kx`)

Move the paddle with held arrow keys, A/D, or by clicking/dragging inside the
playfield. Space/P or Play pauses and resumes; N/New restarts. Clear forty
bricks to advance. Three lives are available, and the top row needs two hits
from level two onward. Pausing preserves the ball position. Losing focus or
closing the window pauses play and removes the animation timer. Reopening
resumes the saved board in a paused state; scores are kept for this boot only.

Collision code uses 8.8 fixed-point positions and subdivides each frame into
eight bounded steps to prevent a fast ball passing through bricks.

## Base Converter (`baseconv.kx`)

Click a row to edit in that base; the other three update immediately. Tab
switches rows, Ctrl+A selects the input, Ctrl+V pastes, and each Copy button
copies that row. Optional `0x`, `0b`, and `0o` prefixes must match the selected
base. Values are unsigned 32-bit integers, 0 through 4,294,967,295. Invalid
digits and overflow clear the derived values and explain the problem. Byte
sizes show KiB, MiB and GiB (powers of 1024), with fractions truncated to three
decimal places. Negative numbers and fractions are not accepted.

## Archive Manager (`archive.kx`)

Choose Add for each file, enter the output name in Save as, then Save. The
default is `desktop/files.fpa`. The list shows original and packed byte sizes;
Remove changes the archive, not the source file. Open reads `.fpa` archives or
the shell's existing `.pz` single-file format. Double-clicking either type in
Files also opens it. Saving always writes `.fpa`.

Select a row and Extract selected, or Extract all, then choose a folder on A:.
All chosen names and payload checksums are checked before extraction begins.
Existing destinations are refused. On cancellation or a disk error, the status
reports how many files completed; those files remain available. There is no
claim of atomic extraction or recovery from a physical write failure.

Limits: 32 files and at most 128 KiB per source file. The archive buffer limit
is 128 KiB below 8 MB RAM, 256 KiB at 8 MB, 512 KiB at 16 MB, and 1 MiB at
32 MB or above. Tier selection rounds the BIOS-reported size up to the next
whole MiB so reserved firmware memory does not put an 8 MB machine in the
4 MB tier. Space is allocated as needed; file data and archive metadata
must fit together in the buffer. Temporary file buffers also need free RAM,
so an operation can report insufficient memory before reaching the limit.
The size indicator shows used space and the current computer's archive limit.
Sources and archives may be read from A: or USB; saving and
extraction currently target A:. Files are stored by basename, without folders
or original timestamps. The complete destination path must fit FLOPFS's
23-character limit. Duplicate names, traversal paths and invalid headers are
refused. Save asks before replacing an existing archive.

An unsaved archive stays in memory if its window closes, and returns when
reopened. Save and close to release its buffer. New discards it after a prompt.
Unsaved data does not survive a restart.

### FPA1 format

All integers are little-endian. Header: four bytes `FPA1`, then a 32-bit file
count (0..32). Each consecutive record contains a 24-byte NUL-terminated name,
a 32-bit packed-payload size, and that many bytes of a PZ1 container. No trailing
bytes are permitted. Names use simple printable characters and no separators,
drive prefixes or leading dots; case-insensitive duplicates are rejected.

PZ1 is the existing `src/lz.inc` format: 12-byte header, original size, 16-bit
rotate/add checksum and a stored/LZSS flag, followed by the payload. The decoder
now also rejects unknown flags, nonzero reserved bytes and trailing bytes.
Archive Manager validates original sizes before allocating or decoding.

## Text Web (`textweb.kx`)

Enter `gopher://host/1` for a menu, `gopher://host/0selector` for a text file, or
`http://host/path`. Optional ports are accepted, for example
`gopher://10.0.2.2:17070/1` with a local QEMU test server. Ctrl+L selects the
address; Enter/Go fetches. Links opens the link list; click a link or use arrows
and Enter. Gopher search entries prompt for words. Back/Forward retain eight
addresses and refetch pages to conserve memory. Scroll text with the wheel,
arrows, Page Up/Down, Home/End or the scrollbar. Escape stops a transfer.

The reader supports Gopher menus, documents and search (types 1, 0, 7), and
HTML/text links through type h. It follows the selector and menu conventions
in [RFC 1436](https://www.rfc-editor.org/rfc/rfc1436) and the percent-encoded URL
form in [RFC 4266](https://www.rfc-editor.org/rfc/rfc4266). Plain HTTP is available;
HTTPS, images, scripts, forms, downloads and automatic redirects are not.
HTTP bodies starting with `<` are treated as basic HTML: tags and script/style
contents are removed, common entities decoded, and relative links resolved.
This is a text reader rather than a full HTML browser.

Limits: 255-byte addresses, 64 KiB received pages, 24 KiB displayed text,
48 links and eight history entries. Limits and failed transfers are reported;
failed transfers preserve the previous page. Copy uses the system's 4 KiB
clipboard and says when only the first 4095 characters fit. Pages and history
stay in RAM. No requests are made when the app first opens.

`net.kx` publishes `net.text`, a versioned table in `src/nettext.h`, for bounded
raw TCP requests. KAPI stays at version 32. TCP transfers are serialized;
received bytes enter an 8 KiB queue and the requesting thread calls the sink.
The advertised TCP window follows queue space and reopens after consumption.
Short interrupt-protected sections keep foreground transfers and background
polling from interleaving NIC register access or TCP state changes. ARP requests
retry during their existing deadline.
This avoids invoking an app callback from another app's address space. HTTP
reports cancellation, truncation and receive errors as failure.

## Validation and maintenance

Shared cores are in `src/{charmap,merge,breakout,base,archive,textweb}_core.inc` and tests
in `src/newapps_tests.inc`. `tools/appcoretests.c` runs them natively;
`kexts/selftest.c` includes them in the full guest suite. For 4 MB, build the
legacy suite with `SELFTEST_SMALL` and run `kexts/appcoretest.c` separately:
the combined test extension itself exceeds the small extension arena.

`tools/newappstests.py` uses its own QEMU process, a disposable image, and
localhost Gopher/HTTP servers on separate temporary ports. It exercises repeated
resizing, clipboard bytes, Notes/Editor, the converter, game input, archive
creation/extraction, legacy PZ, damaged-data and overwrite refusal, real network
streaming, Gopher search, links, history, missing GDI and a 4 MB configuration.
It never stops unrelated
VMs. Fixtures (`newappstest`, `appcoretest`, `importfixture`, `selftest`,
`selftest_small`) are
test-only and must never be added to build.sh's production list.

Both build tools reject undefined imports. The loader checks SHN_UNDEF before
ordinary section indices; the regression fixture verifies E41 before entry.
Every completed build stages the image, kernel update and all production kexts
in `built/`, as required by AGENTS.md. Run `python tools/checkbuild.py` to compare
the complete staged set with the outputs and the bytes installed in the image,
check the kernel boot checksum, and reject missing or extra extensions.

The focused host suites `tools/archivelimittests.c` and
`tools/editlimits_tests.c` cover the 128 KiB file boundary, memory tiers,
allocation failures, and oversized-file protection. Run
`python tools/archivelimitgui.py --memory 4` and `--memory 8` to exercise
archive save/reopen/extract on disposable QEMU images; both scenarios also
edit and save a full 128 KiB file. Build `kexts/newappstest.c` first with
`tools/mkkext.sh`.
