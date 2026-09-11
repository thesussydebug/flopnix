#!/usr/bin/env python3
'Convert a QEMU PPM screenshot to PNG.'
import sys, zlib, struct, os, glob

def chunk(tag, data):
    return (struct.pack('>I', len(data)) + tag + data +
            struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

def convert(src, dst):
    with open(src, 'rb') as f:
        raw = f.read()
    if raw[:2] != b'P6':
        raise SystemExit('%s is not a P6 ppm' % src)

    fields, i = [], 2
    while len(fields) < 3:
        while i < len(raw) and raw[i:i + 1].isspace():
            i += 1
        if raw[i:i + 1] == b'#':
            while raw[i:i + 1] not in (b'\n', b''):
                i += 1
            continue
        j = i
        while j < len(raw) and not raw[j:j + 1].isspace():
            j += 1
        fields.append(int(raw[i:j]))
        i = j
    i += 1
    w, h, _ = fields
    px = raw[i:]
    rows = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
           chunk(b'IDAT', zlib.compress(rows, 6)) +
           chunk(b'IEND', b''))
    with open(dst, 'wb') as f:
        f.write(png)
    print('%s -> %s (%dx%d)' % (os.path.basename(src), os.path.basename(dst), w, h))

if __name__ == '__main__':
    args = sys.argv[1:]
    if not args:
        raise SystemExit('usage: ppm2png.py <file.ppm|dir> ...')
    for a in args:
        for p in (sorted(glob.glob(os.path.join(a, '*.ppm')))
                  if os.path.isdir(a) else [a]):
            convert(p, os.path.splitext(p)[0] + '.png')
