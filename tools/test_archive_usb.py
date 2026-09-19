import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from fscp import do_copy
from usb import make

root = Path(__file__).resolve().parents[1]
os.chdir(root)
qemu = shutil.which('qemu-system-i386')
if not qemu:
    qemu = next((str(p) for p in [Path('C:/msys64/clang64/bin/qemu-system-i386.exe'),
                                Path('C:/Program Files/qemu/qemu-system-i386.exe')] if p.exists()), None)
if not qemu:
    raise SystemExit('qemu-system-i386 is required')
probe = root/'tools/archiveusbtest.kx'
if not probe.exists():
    raise SystemExit('Build tools/archiveusbtest.c with tools/mkkext.sh first')
for memory in (4, 32):
    folder = root/'out'/f'archive-usb-{memory}'
    folder.mkdir(parents=True, exist_ok=True)
    floppy, usb, serial = [folder/n for n in ('floppy.img', 'usb.img', 'serial.txt')]
    shutil.copyfile(root/'built/flopnix.img', floppy)
    do_copy(str(floppy), str(probe), 'sys/usbtest.kx')
    make(str(usb), 10)
    serial.write_text('')
    args = [qemu, '-cpu', 'pentium2', '-m', str(memory), '-boot', 'a',
            '-drive', f'if=floppy,format=raw,file={floppy.as_posix()}',
            '-device', 'piix3-usb-uhci,id=uhci',
            '-drive', f'if=none,id=stick,format=raw,file={usb.as_posix()}',
            '-device', 'usb-storage,bus=uhci.0,drive=stick',
            '-display', 'none', '-serial', f'file:{serial.as_posix()}']
    with (folder/'qemu.log').open('w') as log:
        process = subprocess.Popen(args, stdout=log, stderr=log,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            deadline = time.monotonic()+90
            while time.monotonic()<deadline and process.poll() is None:
                output = serial.read_text(errors='replace')
                match = re.search(r'ARCHIVE USB: (\d+) pass (\d+) fail', output)
                if match:
                    print(f'{memory} MiB: {output.strip()}', flush=True)
                    if int(match[2]):
                        raise SystemExit('Archive USB tests failed')
                    break
                time.sleep(.2)
            else:
                raise SystemExit(f'Archive USB tests did not complete: {serial.read_text(errors="replace")}')
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
