'Compare release files with the build output and floppy contents.'
from pathlib import Path
import hashlib
import re
import shutil
import struct
import subprocess
import tempfile
from fscp import entries

root = Path(__file__).resolve().parents[1]
names = re.search(r'for n in ([\w ]+); do', (root/'build.sh').read_text()).group(1).split()
expected = {name+'.kx' for name in names}
actual = {p.name for p in (root/'built/kexts').glob('*.kx')}
assert actual == expected, ('built/kexts', actual ^ expected)

image = (root/'built/flopnix.img').read_bytes()
kernel = (root/'built/flopnix.ku').read_bytes()
assert len(image) == 1474560
assert image[:512] == (root/'out/boot.bin').read_bytes()
assert image[512:512+len(kernel)] == kernel
assert len(kernel) % 512 == 0 and len(kernel) <= 255*512
assert struct.unpack_from('<H', kernel, 6)[0] == len(kernel)//512
assert struct.unpack_from('<H', image, 506)[0] == len(kernel)//512
check = bytearray(kernel)
saved = struct.unpack_from('<H', check, 4)[0]
check[4:6] = b'\0\0'
checksum = 0
for (word,) in struct.iter_unpack('<H', check):
    checksum = (((checksum << 1) | (checksum >> 15)) + word) & 65535
assert checksum == saved, 'Kernel boot checksum mismatch'

objcopy = shutil.which('llvm-objcopy')
if not objcopy and Path('C:/msys64/clang64/bin/llvm-objcopy.exe').is_file():
    objcopy = 'C:/msys64/clang64/bin/llvm-objcopy.exe'
if not objcopy:
    raise SystemExit('llvm-objcopy not found; use the build toolchain to verify artifacts.')

with tempfile.TemporaryDirectory(prefix='flopnix-check-') as tmp:
    output = Path(tmp)/'check.bin'
    subprocess.run([objcopy, '-O', 'binary', '-j', '.stub', '-j', '.text',
                    '-j', '.rodata', '-j', '.data', str(root/'out/kernel.elf'),
                    str(output)], check=True)
    raw = bytearray(output.read_bytes())
    raw.extend(b'\0' * (-len(raw) % 512))
    # The build stamps these fields after linking.
    raw[4:8] = kernel[4:8]
    assert raw == kernel, 'Kernel update differs from the linked kernel'
    for name in sorted(expected):
        subprocess.run([objcopy, '--strip-debug',
                        str(root/'out'/Path(name).with_suffix('.kxo')),
                        str(output)], check=True)
        assert output.read_bytes() == (root/'built/kexts'/name).read_bytes(), name

files = {e['name']: e for e in entries(image) if e['used']}
assert {n for n in files if n.endswith('.kx')} == {'sys/'+n for n in expected}
for name in sorted(expected):
    built = (root/'built/kexts'/name).read_bytes()
    entry = files['sys/'+name]
    offset = entry['start']*512
    assert image[offset:offset+entry['size']] == built, name
print(f'PASS: image, kernel update, boot checksum, and all {len(expected)} production extensions match.')
for name in ('flopnix.img', 'flopnix.ku'):
    data = (root/'built'/name).read_bytes()
    print(f'{name}: {len(data)} bytes; SHA256 {hashlib.sha256(data).hexdigest()}')
