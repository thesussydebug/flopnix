# Host tools

Run Python commands from the project folder in a shell with Python 3 on PATH.

## Floppy files

```sh
python tools/fscp.py built/flopnix.img --list
python tools/fscp.py working.img hello.txt docs/hello.txt
python tools/fsrename.py working.img old.txt new.txt
```

Use a working image copy when experimenting. `fscp.py` writes directly to
FLOPFS and can replace an existing destination file.

## USB images

```sh
python tools/usb.py make usb.img --mb 10
python tools/usb.py ls usb.img
python tools/usb.py put usb.img song.mid SONG.MID
python tools/usb.py get usb.img SONG.MID saved.mid
python tools/usb.py mkdir usb.img MUSIC
python tools/usb.py putin usb.img MUSIC song.mid SONG.MID
python tools/usb.py lsdir usb.img MUSIC
python tools/usb.py rm usb.img SONG.MID
python tools/usblfn.py usb.img song.mid "My Song.mid"
```

`make` replaces its destination with an empty FAT16 image. The ordinary
USB tool stores DOS-style 8.3 names; `usblfn.py` writes long-name entries.
The QEMU launcher attaches a root `usb.img` only when it exists.

## Physical floppy writing

From PowerShell:

```powershell
powershell -File tools/write-floppy.ps1 -Image built/flopnix.img
```

This writes a real floppy and replaces its contents. Read the helper's
parameters before selecting a different drive.

## Extensions and screenshots

```sh
bash tools/mkkext.sh kexts/hello.c
python tools/ppm2png.py screen.ppm screen.png
```

The extension helper places `.kx` beside its source. An optional image
argument installs the extension into that image. Production builds use the
explicit list in `build.sh`; examples and test probes are separate.
