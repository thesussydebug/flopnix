#!/usr/bin/env python
'Usage: fscp.py IMAGE SOURCE [DEST], or fscp.py IMAGE --list.'

import sys, os, struct, datetime

SUPER, TABLE, TSECT, DATA, END = 288, 289, 10, 299, 2880
MAGIC, VER, ENTSZ = 0x53465046, 2, 40
NFILES = 128

def load(path):
    with open(path, "rb") as source:
        data = bytearray(source.read())
    if len(data) != END * 512:
        raise ValueError('Expected a complete 1.44 MB FLOPNIX image.')
    return data

def read_table(d):
    return d[TABLE*512 : TABLE*512 + NFILES*ENTSZ]

def ensure_fs(d):
    sb = d[SUPER*512 : SUPER*512+12]
    magic, ver, ent = struct.unpack("<III", sb)
    if magic != MAGIC or ver != VER or ent != ENTSZ:
        if any(d[SUPER*512:DATA*512]):
            raise ValueError('Unrecognized FLOPFS metadata; image left unchanged.')
        struct.pack_into("<III", d, SUPER*512, MAGIC, VER, ENTSZ)
        for i in range(NFILES):
            struct.pack_into("<I", d, TABLE*512 + i*ENTSZ + 24, 0)
            d[TABLE*512 + i*ENTSZ + 36] = 0
    live = []
    for e in entries(d):
        if not e['used']:
            continue
        if e['used'] != 1 or not e['name'] or len(e['name']) >= 24:
            raise ValueError('Invalid FLOPFS file table; image left unchanged.')
        if e['attr'] & 1:
            valid = e['size'] == e['start'] == e['nsect'] == 0
        else:
            valid = DATA <= e['start'] < END and 0 < e['nsect'] <= END-e['start'] and e['size'] <= e['nsect']*512
        if not valid:
            raise ValueError('Invalid FLOPFS allocation; image left unchanged.')
        for other in live:
            if e['name'] == other['name'] or (e['nsect'] and other['nsect'] and e['start'] < other['start']+other['nsect'] and other['start'] < e['start']+e['nsect']):
                raise ValueError('Conflicting FLOPFS entries; image left unchanged.')
        live.append(e)

def entries(d):
    t = read_table(d)
    out = []
    for i in range(NFILES):
        e = t[i*ENTSZ:(i+1)*ENTSZ]
        name = e[:24].split(b"\0")[0].decode("latin1")
        size, mtime, start, nsect, used, attr = struct.unpack("<IIHHBB", e[24:38])
        out.append(dict(i=i, name=name, size=size, start=start,
                        nsect=nsect, used=used, attr=attr))
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

def encode_name(name):
    encoded = name.encode('latin1')
    if not 1 <= len(encoded) <= 23 or b'\0' in encoded:
        raise ValueError('FLOPFS names must be 1-23 bytes with no NUL characters.')
    return encoded

def do_copy(img, src, dstname):
    encoded = encode_name(dstname)
    d = load(img)
    ensure_fs(d)
    with open(src, "rb") as source:
        data = source.read()

    nsect = max(1, (len(data) + 511) // 512)

    ents = entries(d)
    if any(e['used'] and e['name'] == dstname and e['attr'] & 1 for e in ents):
        raise ValueError('Cannot replace a FLOPFS folder with a file.')
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
                     encoded, len(data), dt, start, nsect, 1, 0)
    d[start*512 : start*512 + len(data)] = data

    tail = start*512 + len(data)
    pad = (nsect*512) - len(data)
    d[tail:tail+pad] = b"\0" * pad
    with open(img, "wb") as target:
        target.write(d)
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
