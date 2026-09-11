#!/usr/bin/env python
'Usage: fscp.py IMAGE SOURCE [DEST], or fscp.py IMAGE --list.'

import sys, os, struct, datetime

SUPER, TABLE, TSECT, DATA, END = 288, 289, 10, 299, 2880
MAGIC, VER, ENTSZ = 0x53465046, 2, 40
NFILES = 128

def load(path):
    return bytearray(open(path, "rb").read())

def read_table(d):
    return d[TABLE*512 : TABLE*512 + NFILES*ENTSZ]

def ensure_fs(d):
    sb = d[SUPER*512 : SUPER*512+12]
    magic, ver, ent = struct.unpack("<III", sb)
    if magic != MAGIC or ver != VER or ent != ENTSZ:
        struct.pack_into("<III", d, SUPER*512, MAGIC, VER, ENTSZ)
        for i in range(NFILES):
            struct.pack_into("<I", d, TABLE*512 + i*ENTSZ + 24, 0)
            d[TABLE*512 + i*ENTSZ + 36] = 0

def entries(d):
    t = read_table(d)
    out = []
    for i in range(NFILES):
        e = t[i*ENTSZ:(i+1)*ENTSZ]
        name = e[:24].split(b"\0")[0].decode("latin1")
        size, mtime, start, nsect, used, attr = struct.unpack("<IIHHBB", e[24:38])
        out.append(dict(i=i, name=name, size=size, start=start,
                        nsect=nsect, used=used))
    return out

def free_run(d, need, exclude):
    used = [(e["start"], e["nsect"]) for e in entries(d)
            if e["used"] and e["i"] != exclude]
    start = DATA
    while start + need <= END:
        clash = None
        for s, n in used:
            if start < s + n and s < start + need:
                clash = s + n
                break
        if clash is None:
            return start
        start = clash
    return -1

def do_list(img):
    d = load(img)
    for e in entries(d):
        if e["used"]:
            print("  %-24s %7d bytes  LBA %d" % (e["name"], e["size"], e["start"]))

def do_copy(img, src, dstname):
    d = load(img)
    ensure_fs(d)
    data = open(src, "rb").read()

    nsect = max(1, (len(data) + 511) // 512)

    ents = entries(d)
    slot = next((e["i"] for e in ents if e["used"] and e["name"] == dstname), None)
    if slot is None:
        slot = next((e["i"] for e in ents if not e["used"]), None)
        if slot is None:
            sys.exit("directory full")

    start = free_run(d, nsect, slot)
    if start < 0:
        sys.exit("no free space for %d sectors" % nsect)

    t = datetime.datetime.now()
    dt = ((((t.year-1980) << 9) | (t.month << 5) | t.day) << 16) | \
         ((t.hour << 11) | (t.minute << 5) | (t.second // 2))
    off = TABLE*512 + slot*ENTSZ
    struct.pack_into("<24sIIHHBB2x", d, off,
                     dstname.encode("latin1"), len(data), dt, start, nsect, 1, 0)
    d[start*512 : start*512 + len(data)] = data

    tail = start*512 + len(data)
    pad = (nsect*512) - len(data)
    d[tail:tail+pad] = b"\0" * pad
    open(img, "wb").write(d)
    print("copied %s -> %s:%s (%d bytes, LBA %d)" %
          (src, os.path.basename(img), dstname, len(data), start))

if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 2 and a[1] == "--list":
        do_list(a[0])
    elif len(a) in (2, 3):
        img, src = a[0], a[1]
        dst = a[2] if len(a) == 3 else os.path.basename(src)
        do_copy(img, src, dst)
    else:
        print(__doc__)
        sys.exit(1)
