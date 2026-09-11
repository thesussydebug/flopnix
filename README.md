# FLOPNIX

An operating system built from scratch for x86 PCs. The kernel, desktop, shell, and 40 extensions fit on a
1.44 MB floppy image.

FLOPNIX has a windowed desktop, a Unix-style shell, floppy and USB file
management, a text editor, Paint, MIDI playback, network tools, and games.
Applications and services are loadable `.kx` extensions. The reference CPU
is a Pentium II; QEMU is used for development and regression tests.

## Run it

Build from source or unpack the release files into `built/`, then run:

```bat
run.bat
```

The launcher uses QEMU with 64 MB RAM and an NE2000 network adapter. It finds
QEMU in MSYS2, Program Files, or your PATH. `run.bat lan-test` selects a
private 192.168.76.0/24 test subnet. Normal QEMU user networking uses its own
virtual subnet, not the host's LAN address range.

USB storage is optional. To create a new, empty test stick from the MSYS2
shell, run this once when `usb.img` does not already exist:

```sh
python tools/usb.py make usb.img --mb 10
```

The launcher attaches `usb.img` when present. It is local writable data and
is excluded from Git. The image-making command replaces its destination.

For physical media, write `built/flopnix.img` as a raw disk image. The
PowerShell helper is documented in [the tools guide](docs/TOOLS.md).
FLOPFS is not FAT: other operating systems cannot browse its files directly.

## Build

The development setup uses Windows with MSYS2 and its Clang64 toolchain:
Clang, LLD, LLVM tools, NASM, Python 3, and the usual shell utilities. Install
QEMU to run the guest tests. From a Git Bash or MSYS2 shell:

```sh
bash build.sh
```

Each successful build refreshes the complete delivery set:

| File | Purpose |
| --- | --- |
| `built/flopnix.img` | Full floppy image for a fresh installation |
| `built/flopnix.ku` | Kernel update for an existing FLOPNIX disk |
| `built/kexts/*.kx` | All matching production extensions |

`built/` is the only location for finished release files. `out/` holds
compiler intermediates and test output. Generated files are excluded from
Git; attach the contents of `built/` to a GitHub release.

The build checks documentation versions, command usage, extension imports,
and the installed extension count. The kernel must fit within 255 sectors.
Test extensions are built separately and are not shipped.

## Test

After building, run the artifact and API checks from the MSYS2 shell:

```sh
python tools/checkbuild.py
python tools/test_kapidoc.py
```

Run the guest suite from PowerShell on both CPU profiles:

```powershell
powershell -File tools/runtests.ps1 -Cpu pentium2
powershell -File tools/runtests.ps1 -Cpu qemu32
```

The test runner boots an image copy and captures results over the serial
port. Test results must end with a completed suite and zero failures.
Targeted graphics, networking, file-transfer, and crash tests are described
in [Maintaining FLOPNIX](docs/MAINTAINING.md).

## Project layout

| Folder | Contents |
| --- | --- |
| `boot/` | BIOS boot sector and protected-mode entry stub |
| `src/` | Kernel and shared code |
| `kexts/` | Apps, services, examples, and test sources |
| `tools/` | Build helpers, disk tools, and test runners |
| `docs/` | Architecture, filesystem, app, and maintenance guides |
| `built/` | Generated files ready to copy or release |
| `out/` | Generated build and test output |

## Development

Start with the [documentation index](docs/README.md) and
[contribution guide](CONTRIBUTING.md). The app tutorial is in
[Writing apps](docs/WRITING-APPS.md).

Code comments are sparse: one short line about what the code does or a
constraint the caller needs. Longer explanations belong in the docs.

This is an experimental OS. Supported hardware and remaining limits are
covered in the architecture and networking guides. Emulator test results
do not establish compatibility with every physical PC or USB device.
