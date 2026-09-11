# Desktop folders and compact utility windows

The desktop shows real files and folders below `desktop/`. Right-click empty
space and choose New folder, then enter a name. Open a folder to browse it in
Files. Drag files onto a desktop folder to move them there; drag a folder from
Files onto empty desktop space to move the whole folder. Same-floppy moves
rename paths without copying file data. Multiple selected files can be dragged
together. Copy and Paste also support folders, including nested folders and
empty directories between floppy and USB. System-folder moves are refused.
USB folder Cut/Paste is refused to avoid deleting a partly copied source; use
Copy and verify the destination first. Desktop selection receives keyboard
focus, so folder renaming works while other windows remain open.

Desktop and Files share Copy (`Ctrl+C`), Cut (`Ctrl+X`) and Paste (`Ctrl+V`).
Paste onto empty desktop space stores files in `desktop/`; a folder's context
menu pastes into that folder. Multiple selected files work in either direction.
Dragging moves items on the same drive and copies between drives. Files can
create folders inside existing floppy folders from either the File menu or
the context menu. On the desktop, Enter opens the selection and `Ctrl+R`
starts renaming it.

A failed Cut/Paste keeps only the failed items on the clipboard for retry.
Existing destination files are never overwritten. The first failure stays in
the result message even when later items succeed, and Files uses the full
status-bar width for that message. Copy/Paste within the same folder creates
a separate copy; dropping an item back into its own folder does nothing.

USB transfers use the source's filename, not its original folder path. USB
files can be moved between folders by copying successfully before deleting
the source. New FAT filenames use 8.3 names; longer names receive distinct
aliases such as `PACKAG~1.FPA` and `PACKAG~2.FPA`, shown in the result message.
Transfers use a private buffer sized to the source and reject incomplete
reads or insufficient memory instead of writing truncated files. A copy needs
one contiguous free heap block slightly larger than the file. In particular,
a 4 MB machine can refuse larger archives even when the disk has room; adding
RAM allows a larger copy buffer. Same-floppy moves need no file-sized buffer.
Completed disk operations extend the app watchdog deadline, so a long but
progressing USB copy does not open a misleading end-task prompt.

Folder copies involving USB are bounded to eight directory levels (including
the copied root) and 128 children per folder. FLOPFS path limits still apply.
An error can leave a partial destination folder, but never deletes the source.
Remove that incomplete destination before retrying the folder copy.

FLOPFS still allows only 23 characters for the complete path. Short names are
especially useful inside desktop folders. A rename that would make any child
path too long is refused. Nonempty folders must be emptied before deletion.
Archive files (`.fpa` and `.pz`) use a dedicated chest icon in both desktop and
Files views.

Settings automatically fits the selected Display, Mouse, Network, or Wallpaper
page, including the individual network and wallpaper subpages. System Info has
a compact layout and no introductory tagline. Its version is rendered by the
running kernel from `OS_NAME` and `OS_VER` in `src/apps.c`; there is no separate
System Info extension version to become stale after a kernel update.

`tools/polishtests.py` exercises the shipping windows and folder operations on
a disposable QEMU image. `src/newapps_tests.inc` covers the Breakout collision
core; the shared folder and save-path cases are also in the guest self-tests.

Validated in QEMU at 640x480 with 4 MB, and with GDI removed from a disposable
32 MB image: compact window sizes, all Settings subpages, Breakout timer cleanup,
desktop folder moves/renames/copies, Files Copy/Paste preserving the source, and
Paint saving through the picker into `desktop/work/`. Final folder runs reported
no recovered faults and compared copied bytes after shutting down their VMs.
The full production guest self-test reported 56,472 passes and zero failures.

Build `kexts/newappstest.c` with `tools/mkkext.sh` before running
`python tools/polishtests.py --memory 4` or
`python tools/polishtests.py --no-gdi`. `--folders-only` repeats the filesystem
and nested-save portion without the app-layout checks.

`tools/fileopstests.c` exercises the shared production transfer logic with
simulated read/write/delete failures, name collisions and memory limits.
`python tools/transfertests.py` and `--memory 4` exercise the real Files and
Desktop interfaces using disposable copies of both `built/flopnix.img` and
`usb.img`. They compare binary file contents after transfers and verify that
the original USB image has not changed.

`python tools/transfertests.py --qol-only --memory 4` checks the Cut/Paste
buttons, nested folder creation, empty USB folders, and Paste into a desktop
folder. `--large-only` isolates large-file copying; at 4 MB it verifies an
explicit RAM-shortage refusal with the source preserved. The ordinary 32 MB
run verifies the entire 320 KiB binary copy and absence of a hang prompt.

Transfer validation (2026-09-10): the final production build passed the full
Files/Desktop/USB sequence at 32 MB and 4 MB, plus the focused 4 MB folder and
toolbar checks. Two independent 320 KiB copies at 32 MB matched every byte,
reported zero USB transport resets and raised no hang prompt. The 4 MB large
file case reported insufficient RAM, kept the source, and remained responsive.
The original USB image was unchanged. Production guest self-tests reported
56,654 passes and zero failures; the native transfer harness passed 30 checks.
The image, kernel update, and all 40 production extensions were verified as a
matching set in `built/`.

If no local usb.img exists, the transfer runner creates an empty disposable
FAT16 image for its fixtures. An existing local USB image is copied and checked
for changes after the run.
