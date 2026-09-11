#!/usr/bin/env python
'Usage: usblfn.py IMAGE FILE "Long Name" [ALIAS], or IMAGE --dump.'

import sys, struct

SECSZ = 512

def load(p): return bytearray(open(p, "rb").read())

def mount(d):
    'Locate a FAT16 volume and read its geometry.'
    base = 0
    if d[510] == 0x55 and d[511] == 0xAA and d[0x1C2] != 0:
        for i in range(4):
            pe = d[0x1BE + i*16 : 0x1BE + (i+1)*16]
            if pe[4] in (0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E):
                base = struct.unpack("<I", pe[8:12])[0]
                break
    b = d[base*SECSZ : base*SECSZ + SECSZ]
    spc      = b[13]
    reserved = struct.unpack("<H", b[14:16])[0]
    nfat     = b[16]
    rootents = struct.unpack("<H", b[17:19])[0]
    fatsz    = struct.unpack("<H", b[22:24])[0] or struct.unpack("<I", b[36:40])[0]
    fat_lba  = base + reserved
    root_lba = fat_lba + nfat*fatsz
    root_secs = ((rootents*32) + SECSZ - 1)//SECSZ
    data_lba = root_lba + root_secs
    return dict(base=base, spc=spc, fat_lba=fat_lba, nfat=nfat, fatsz=fatsz,
                root_lba=root_lba, root_secs=root_secs, rootents=rootents,
                data_lba=data_lba)

def checksum(sfn):
    s = 0
    for c in sfn:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s

def to83(alias):
    base, _, ext = alias.upper().partition(".")
    return (base[:8].ljust(8) + ext[:3].ljust(3)).encode("latin1")

def fat_get(d, m, c):
    off = m["fat_lba"]*SECSZ + c*2
    return struct.unpack("<H", d[off:off+2])[0]

def fat_set(d, m, c, v):
    for f in range(m["nfat"]):
        off = (m["fat_lba"] + f*m["fatsz"])*SECSZ + c*2
        struct.pack_into("<H", d, off, v)

def alloc(d, m, n):
    out, c = [], 2
    total = (len(d)//SECSZ - m["data_lba"])//m["spc"] + 1
    while len(out) < n and c < total:
        if fat_get(d, m, c) == 0:
            out.append(c)
        c += 1
    if len(out) < n:
        raise SystemExit("usblfn: not enough free clusters")
    for i, c in enumerate(out):
        fat_set(d, m, c, 0xFFFF if i == len(out)-1 else out[i+1])
    return out

def put(img, host, lname, alias):
    d = load(img)
    m = mount(d)
    data = open(host, "rb").read()
    sfn = to83(alias)
    cs = checksum(sfn)

    frags = max(1, (len(lname) + 12)//13)
    if frags > 20: raise SystemExit("usblfn: name too long")
    need = frags + 1

    root0 = m["root_lba"]*SECSZ
    nslots = m["rootents"]
    start = None
    run = 0
    for i in range(nslots):
        e = d[root0 + i*32]
        if e in (0x00, 0xE5):
            run += 1
            if run == need:
                start = i - need + 1
                break
        else:
            run = 0
    if start is None: raise SystemExit("usblfn: no room in the root directory")

    bpc = m["spc"]*SECSZ
    nclus = (len(data) + bpc - 1)//bpc
    chain = alloc(d, m, nclus) if nclus else []
    for i, c in enumerate(chain):
        lba = m["data_lba"] + (c-2)*m["spc"]
        chunk = data[i*bpc : (i+1)*bpc].ljust(bpc, b"\0")
        d[lba*SECSZ : lba*SECSZ + bpc] = chunk

    SLOT = [1,3,5,7,9,14,16,18,20,22,24,28,30]
    for f in range(frags, 0, -1):
        e = bytearray(32)
        e[0] = f | (0x40 if f == frags else 0)
        e[11] = 0x0F
        e[13] = cs
        for ci in range(13):
            idx = (f-1)*13 + ci
            if idx < len(lname):  w = ord(lname[idx])
            elif idx == len(lname): w = 0
            else: w = 0xFFFF
            struct.pack_into("<H", e, SLOT[ci], w)
        off = root0 + (start + (frags - f))*32
        d[off:off+32] = e

    e = bytearray(32)
    e[0:11] = sfn
    e[11] = 0x20
    struct.pack_into("<H", e, 26, chain[0] & 0xFFFF if chain else 0)
    struct.pack_into("<I", e, 28, len(data))
    struct.pack_into("<H", e, 22, 0x6000)
    struct.pack_into("<H", e, 24, 0x5CE8)
    off = root0 + (start + frags)*32
    d[off:off+32] = e

    open(img, "wb").write(d)
    print('wrote "%s" (alias %s, %d bytes, %d LFN frags, csum %d)'
          % (lname, alias, len(data), frags, cs))

def dump(img):
    d = load(img); m = mount(d)
    root0 = m["root_lba"]*SECSZ
    for i in range(m["rootents"]):
        e = d[root0 + i*32 : root0 + (i+1)*32]
        if e[0] == 0x00: break
        kind = "LFN " if e[11] == 0x0F else ("del " if e[0] == 0xE5 else "8.3 ")
        if e[11] == 0x0F:
            print("%3d %s ord=%02x csum=%d" % (i, kind, e[0], e[13]))
        else:
            print("%3d %s %-11s size=%d" % (i, kind,
                  e[0:11].decode("latin1"), struct.unpack("<I", e[28:32])[0]))

if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[2] == "--dump":
        dump(sys.argv[1])
    elif len(sys.argv) >= 4:
        img, host, lname = sys.argv[1], sys.argv[2], sys.argv[3]
        alias = sys.argv[4] if len(sys.argv) > 4 else "LONG~1.DAT"
        put(img, host, lname, alias)
    else:
        print(__doc__ or "usage: usblfn.py usb.img HOSTFILE \"Long Name\" [ALIAS]")
