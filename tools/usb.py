#!/usr/bin/env python
'Usage: usb.py make IMAGE [--mb 10], or usb.py COMMAND IMAGE [ARGS].'
import os
import struct
import sys

SEC = 512
PART_LBA = 2048
DIRENT = 32

def _u16(b, o): return struct.unpack_from('<H', b, o)[0]
def _u32(b, o): return struct.unpack_from('<I', b, o)[0]

class Fat16:
    'Read and edit a partitioned FAT16 image.'

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = bytearray(f.read())
        self.base = self._find_partition() * SEC
        b = self.data
        o = self.base
        if _u16(b, o + 11) != SEC:
            raise SystemExit('not a 512-byte-sector FAT volume')
        self.spc = b[o + 13]
        self.reserved = _u16(b, o + 14)
        self.nfat = b[o + 16]
        self.rootents = _u16(b, o + 17)
        self.fatsz = _u16(b, o + 22)
        tot = _u16(b, o + 19) or _u32(b, o + 32)
        self.fat0 = o + self.reserved * SEC
        self.rootoff = self.fat0 + self.nfat * self.fatsz * SEC
        self.dataoff = self.rootoff + self.rootents * DIRENT
        self.clusters = (tot - self.reserved - self.nfat * self.fatsz -
                         (self.rootents * DIRENT) // SEC) // self.spc

    def _find_partition(self):
        b = self.data
        if b[510] == 0x55 and b[511] == 0xAA and b[0x1C2] != 0:
            for i in range(4):
                pe = 0x1BE + i * 16
                if b[pe + 4] in (0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E):
                    return _u32(b, pe + 8)
        return 0

    def _fat_get(self, n):
        return _u16(self.data, self.fat0 + n * 2)

    def _fat_set(self, n, v):
        for i in range(self.nfat):
            struct.pack_into('<H', self.data,
                             self.fat0 + i * self.fatsz * SEC + n * 2, v)

    def _free_cluster(self):
        for n in range(2, self.clusters + 2):
            if self._fat_get(n) == 0:
                return n
        raise SystemExit('usb image is full')

    def _cluster_off(self, n):
        return self.dataoff + (n - 2) * self.spc * SEC

    def entries(self):
        out = []
        for i in range(self.rootents):
            o = self.rootoff + i * DIRENT
            first = self.data[o]
            if first == 0x00:
                break
            if first == 0xE5 or (self.data[o + 11] & 0x0F) == 0x0F:
                continue
            name = bytes(self.data[o:o + 8]).decode('latin-1').rstrip()
            ext = bytes(self.data[o + 8:o + 11]).decode('latin-1').rstrip()
            out.append({
                'idx': i, 'off': o,
                'name': name + ('.' + ext if ext else ''),
                'attr': self.data[o + 11],
                'clus': _u16(self.data, o + 26),
                'size': _u32(self.data, o + 28),
            })
        return out

    def find(self, name):
        want = name.upper()
        for e in self.entries():
            if e['name'].upper() == want:
                return e
        return None

    def _free_dirent(self):
        for i in range(self.rootents):
            o = self.rootoff + i * DIRENT
            if self.data[o] in (0x00, 0xE5):
                return o
        raise SystemExit('root directory is full')

    @staticmethod
    def _to83(name):
        base, _, ext = os.path.basename(name).rpartition('.')
        if not base:
            base, ext = ext, ''
        keep = lambda s: ''.join(c for c in s.upper()
                                 if c.isalnum() or c in "_-~!@#$%^&()'{}")
        b, e = keep(base)[:8], keep(ext)[:3]
        return b.ljust(8), e.ljust(3), (b + ('.' + e if e else ''))

    def put(self, hostpath, asname=None):
        with open(hostpath, 'rb') as f:
            payload = f.read()
        b8, e3, shown = self._to83(asname or hostpath)
        old = self.find(shown)
        if old:
            self.rm(shown)
        need = max(1, (len(payload) + self.spc * SEC - 1) // (self.spc * SEC))
        chain = []
        for _ in range(need):
            c = self._free_cluster()
            self._fat_set(c, 0xFFFF)
            chain.append(c)
        for i, c in enumerate(chain):
            self._fat_set(c, 0xFFFF if i == len(chain) - 1 else chain[i + 1])
            off = self._cluster_off(c)
            part = payload[i * self.spc * SEC:(i + 1) * self.spc * SEC]
            self.data[off:off + len(part)] = part
            pad = self.spc * SEC - len(part)
            if pad:
                self.data[off + len(part):off + self.spc * SEC] = b'\0' * pad
        o = self._free_dirent()
        self.data[o:o + DIRENT] = b'\0' * DIRENT
        self.data[o:o + 8] = b8.encode('latin-1')
        self.data[o + 8:o + 11] = e3.encode('latin-1')
        self.data[o + 11] = 0x20
        struct.pack_into('<H', self.data, o + 26, chain[0])
        struct.pack_into('<I', self.data, o + 28, len(payload))
        struct.pack_into('<H', self.data, o + 22, 0x6000)
        struct.pack_into('<H', self.data, o + 24, 0x5AE9)
        return shown, len(payload)

    def mkdir(self, name):
        'Create a root subdirectory with dot entries.'
        b8, e3, shown = self._to83(name)
        if self.find(shown):
            raise SystemExit('already exists: ' + shown)
        c = self._free_cluster()
        self._fat_set(c, 0xFFFF)
        off = self._cluster_off(c)
        self.data[off:off + self.spc * SEC] = b'\0' * (self.spc * SEC)

        def dirent(at, raw8, raw3, attr, clus):
            self.data[at:at + DIRENT] = b'\0' * DIRENT
            self.data[at:at + 8] = raw8.encode('latin-1')
            self.data[at + 8:at + 11] = raw3.encode('latin-1')
            self.data[at + 11] = attr
            struct.pack_into('<H', self.data, at + 26, clus)
            struct.pack_into('<H', self.data, at + 22, 0x6000)
            struct.pack_into('<H', self.data, at + 24, 0x5AE9)

        dirent(off, '.'.ljust(8), '   ', 0x10, c)
        dirent(off + DIRENT, '..'.ljust(8), '   ', 0x10, 0)
        dirent(self._free_dirent(), b8, e3, 0x10, c)
        return shown

    def put_into(self, dirname, hostpath, asname=None):
        'Write a file into a root subdirectory.'
        d = self.find(dirname)
        if not d or not (d['attr'] & 0x10):
            raise SystemExit('no such directory: ' + dirname)
        with open(hostpath, 'rb') as f:
            payload = f.read()
        b8, e3, shown = self._to83(asname or hostpath)
        dbase = self._cluster_off(d['clus'])
        slot = None
        for i in range(self.spc * SEC // DIRENT):
            if self.data[dbase + i * DIRENT] in (0x00, 0xE5):
                slot = dbase + i * DIRENT
                break
        if slot is None:
            raise SystemExit('directory is full: ' + dirname)
        need = max(1, (len(payload) + self.spc * SEC - 1) // (self.spc * SEC))
        chain = []
        for _ in range(need):
            c = self._free_cluster()
            self._fat_set(c, 0xFFFF)
            chain.append(c)
        for i, c in enumerate(chain):
            self._fat_set(c, 0xFFFF if i == len(chain) - 1 else chain[i + 1])
            off = self._cluster_off(c)
            part = payload[i * self.spc * SEC:(i + 1) * self.spc * SEC]
            self.data[off:off + self.spc * SEC] = part.ljust(self.spc * SEC, b'\0')
        self.data[slot:slot + DIRENT] = b'\0' * DIRENT
        self.data[slot:slot + 8] = b8.encode('latin-1')
        self.data[slot + 8:slot + 11] = e3.encode('latin-1')
        self.data[slot + 11] = 0x20
        struct.pack_into('<H', self.data, slot + 26, chain[0])
        struct.pack_into('<I', self.data, slot + 28, len(payload))
        struct.pack_into('<H', self.data, slot + 22, 0x6000)
        struct.pack_into('<H', self.data, slot + 24, 0x5AE9)
        return shown, len(payload)

    def ls_dir(self, dirname):
        'List a root subdirectory without dot entries.'
        d = self.find(dirname)
        if not d or not (d['attr'] & 0x10):
            raise SystemExit('no such directory: ' + dirname)
        base, out = self._cluster_off(d['clus']), []
        for i in range(self.spc * SEC // DIRENT):
            o = base + i * DIRENT
            if self.data[o] == 0x00:
                break
            if self.data[o] == 0xE5 or (self.data[o + 11] & 0x0F) == 0x0F:
                continue
            nm = bytes(self.data[o:o + 8]).decode('latin-1').rstrip()
            ex = bytes(self.data[o + 8:o + 11]).decode('latin-1').rstrip()
            if nm in ('.', '..'):
                continue
            out.append({'name': nm + ('.' + ex if ex else ''),
                        'attr': self.data[o + 11],
                        'clus': _u16(self.data, o + 26),
                        'size': _u32(self.data, o + 28)})
        return out

    def get(self, name, out):
        e = self.find(name)
        if not e:
            raise SystemExit('no such file: ' + name)
        data, c, left = bytearray(), e['clus'], e['size']
        while 2 <= c < 0xFFF8 and left > 0:
            off = self._cluster_off(c)
            n = min(left, self.spc * SEC)
            data += self.data[off:off + n]
            left -= n
            c = self._fat_get(c)
        with open(out, 'wb') as f:
            f.write(data)
        return len(data)

    def rm(self, name):
        e = self.find(name)
        if not e:
            raise SystemExit('no such file: ' + name)
        c = e['clus']
        while 2 <= c < 0xFFF8:
            nxt = self._fat_get(c)
            self._fat_set(c, 0)
            c = nxt
        self.data[e['off']] = 0xE5
        return e['name']

    def save(self):
        with open(self.path, 'wb') as f:
            f.write(self.data)

def make(path, mb):
    total = mb * 1024 * 1024 // SEC
    if total <= PART_LBA + 64:
        raise SystemExit('too small')
    psec = total - PART_LBA
    spc = 4
    rootents = 512
    rootsec = rootents * DIRENT // SEC
    fatsz = 1
    for _ in range(12):
        clus = (psec - 1 - rootsec - 2 * fatsz) // spc
        need = max(1, (clus * 2 + SEC - 1) // SEC)
        if need == fatsz:
            break
        fatsz = need
    if not (4085 <= clus <= 65524):
        raise SystemExit('cluster count %d is not FAT16' % clus)

    img = bytearray(total * SEC)

    mbr = img
    struct.pack_into('<B', mbr, 0x1BE, 0x80)
    mbr[0x1BE + 1:0x1BE + 4] = b'\x01\x01\x00'
    mbr[0x1BE + 4] = 0x06
    mbr[0x1BE + 5:0x1BE + 8] = b'\xFE\xFF\xFF'
    struct.pack_into('<I', mbr, 0x1BE + 8, PART_LBA)
    struct.pack_into('<I', mbr, 0x1BE + 12, psec)
    mbr[510], mbr[511] = 0x55, 0xAA

    o = PART_LBA * SEC
    img[o:o + 3] = b'\xEB\x3C\x90'
    img[o + 3:o + 11] = b'FLOPNIX '
    struct.pack_into('<H', img, o + 11, SEC)
    img[o + 13] = spc
    struct.pack_into('<H', img, o + 14, 1)
    img[o + 16] = 2
    struct.pack_into('<H', img, o + 17, rootents)
    struct.pack_into('<H', img, o + 19, 0)
    img[o + 21] = 0xF8
    struct.pack_into('<H', img, o + 22, fatsz)
    struct.pack_into('<H', img, o + 24, 63)
    struct.pack_into('<H', img, o + 26, 255)
    struct.pack_into('<I', img, o + 28, PART_LBA)
    struct.pack_into('<I', img, o + 32, psec)
    img[o + 36] = 0x80
    img[o + 38] = 0x29
    struct.pack_into('<I', img, o + 39, 0x464C4F50)
    img[o + 43:o + 54] = b'FLOPNIX USB'
    img[o + 54:o + 62] = b'FAT16   '
    img[o + 510], img[o + 511] = 0x55, 0xAA

    f0 = o + SEC
    for i in range(2):
        fo = f0 + i * fatsz * SEC
        img[fo:fo + 4] = b'\xF8\xFF\xFF\xFF'

    with open(path, 'wb') as f:
        f.write(img)
    return clus, spc * SEC

def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, img = argv[1], argv[2]
    if cmd == 'make':
        mb = 10
        if '--mb' in argv:
            mb = int(argv[argv.index('--mb') + 1])
        clus, csz = make(img, mb)
        print('made %s: %d MB, FAT16, %d clusters of %d bytes'
              % (img, mb, clus, csz))
        return 0

    v = Fat16(img)
    if cmd == 'ls':
        ents = v.entries()
        if not ents:
            print('(empty)')
        for e in ents:
            print('%-14s %8d  %s' % (e['name'], e['size'],
                                     'DIR' if e['attr'] & 0x10 else ''))
        used = sum(1 for n in range(2, v.clusters + 2) if v._fat_get(n))
        print('-- %d file(s), %d/%d clusters used'
              % (len(ents), used, v.clusters))
    elif cmd == 'put':
        name, size = v.put(argv[3], argv[4] if len(argv) > 4 else None)
        v.save()
        print('put %s -> %s:%s (%d bytes)' % (argv[3], img, name, size))
    elif cmd == 'get':
        out = argv[4] if len(argv) > 4 else argv[3]
        n = v.get(argv[3], out)
        print('got %s:%s -> %s (%d bytes)' % (img, argv[3], out, n))
    elif cmd == 'rm':
        n = v.rm(argv[3])
        v.save()
        print('removed %s:%s' % (img, n))
    elif cmd == 'mkdir':
        n = v.mkdir(argv[3])
        v.save()
        print('made %s:%s/' % (img, n))
    elif cmd == 'putin':
        name, size = v.put_into(argv[3], argv[4],
                                argv[5] if len(argv) > 5 else None)
        v.save()
        print('put %s -> %s:%s/%s (%d bytes)' % (argv[4], img, argv[3], name, size))
    elif cmd == 'lsdir':
        ents = v.ls_dir(argv[3])
        if not ents:
            print('(empty)')
        for e in ents:
            print('%-14s %8d  %s' % (e['name'], e['size'],
                                     'DIR' if e['attr'] & 0x10 else ''))
    else:
        print(__doc__)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
