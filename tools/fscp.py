#!/usr/bin/env python
'Usage: fscp.py IMAGE SOURCE [DEST], IMAGE --list, or IMAGE --upgrade OUTPUT.'

import sys, os, struct, datetime

SUPER, TABLE, TSECT, DATA, END = 288, 289, 80, 369, 2880
MAGIC, VER, ENTSZ = 0x53465046, 3, 80
NFILES, NAMELEN = 512, 64
ATTR_DIR = 0x10

def load(path):
    with open(path, "rb") as source:
        data = bytearray(source.read())
    if len(data) != END * 512:
        raise ValueError('Expected a complete 1.44 MB FLOPNIX image.')
    return data

def layout(d):
    magic, ver, ent = struct.unpack_from('<III', d, SUPER*512)
    if magic == MAGIC:
        if (ver, ent) == (2, 40):
            return 128, 24, 40, 299
        if (ver, ent) == (VER, ENTSZ):
            return NFILES, NAMELEN, ENTSZ, DATA
    raise ValueError('Unrecognized FLOPFS metadata; image left unchanged.')

def read_table(d):
    count, _, size, _ = layout(d)
    return d[TABLE*512:TABLE*512+count*size]

def ensure_fs(d):
    if not any(d[SUPER*512:DATA*512]):
        struct.pack_into('<III', d, SUPER*512, MAGIC, VER, ENTSZ)
    count, namelen, size, first = layout(d)
    live = []
    for e in entries(d):
        if not e['used']:
            continue
        if e['used'] != 1 or not e['name'] or len(e['name']) >= namelen:
            raise ValueError('Invalid FLOPFS file table; image left unchanged.')
        if e['attr'] & ATTR_DIR:
            valid = e['size'] == e['start'] == e['nsect'] == 0
        else:
            valid = first <= e['start'] < END and 0 < e['nsect'] <= END-e['start'] and e['size'] <= e['nsect']*512
        if not valid:
            raise ValueError('Invalid FLOPFS allocation; image left unchanged.')
        for other in live:
            if e['name'] == other['name'] or (e['nsect'] and other['nsect'] and e['start'] < other['start']+other['nsect'] and other['start'] < e['start']+e['nsect']):
                raise ValueError('Conflicting FLOPFS entries; image left unchanged.')
        live.append(e)

def entries(d):
    count, namelen, entsz, _ = layout(d)
    t = read_table(d)
    out = []
    for i in range(count):
        e = t[i*entsz:(i+1)*entsz]
        name = e[:namelen].split(b"\0")[0].decode("latin1")
        size, mtime, start, nsect, used, attr = struct.unpack("<IIHHBB", e[namelen:namelen+14])
        out.append(dict(i=i, name=name, size=size, start=start,
                        nsect=nsect, used=used, attr=attr, mtime=mtime))
    return out

def free_run(d, need, exclude):
    used = [(e["start"], e["nsect"]) for e in entries(d)
            if e["used"] and e["i"] != exclude]
    start = layout(d)[3]
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
    ensure_fs(d)
    for e in entries(d):
        if e["used"]:
            print("  %-24s %7d bytes  LBA %d" % (e["name"], e["size"], e["start"]))

def encode_name(name, namelen=NAMELEN):
    encoded = name.encode('latin1')
    if not 1 <= len(encoded) < namelen or b'\0' in encoded:
        raise ValueError(f'FLOPFS paths must be 1-{namelen-1} bytes with no NUL characters.')
    return encoded

def do_copy(img, src, dstname):
    d = load(img)
    ensure_fs(d)
    _, namelen, entsz, _ = layout(d)
    encoded = encode_name(dstname, namelen)
    with open(src, "rb") as source:
        data = source.read()

    nsect = max(1, (len(data) + 511) // 512)

    ents = entries(d)
    if any(e['used'] and e['name'] == dstname and e['attr'] & ATTR_DIR for e in ents):
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
    off = TABLE*512 + slot*entsz
    struct.pack_into(f"<{namelen}sIIHHBB2x", d, off,
                     encoded, len(data), dt, start, nsect, 1, 0)
    d[start*512 : start*512 + len(data)] = data

    tail = start*512 + len(data)
    pad = (nsect*512) - len(data)
    d[tail:tail+pad] = b"\0" * pad
    with open(img, "wb") as target:
        target.write(d)
    print("copied %s -> %s:%s (%d bytes, LBA %d)" %
          (src, os.path.basename(img), dstname, len(data), start))

def do_upgrade(img, output):
    from pathlib import Path
    old = load(img)
    ensure_fs(old)
    release = load(Path(__file__).resolve().parents[1]/'built/flopnix.img')
    ensure_fs(release)
    if layout(release) != (NFILES, NAMELEN, ENTSZ, DATA):
        raise ValueError('Build the current release before upgrading an image.')
    records = {}
    for disk in (old, release):
        for e in entries(disk):
            if e['used']:
                payload = bytes(disk[e['start']*512:(e['start']+e['nsect'])*512])
                records[e['name']] = (e, payload)
    if len(records) > NFILES:
        raise ValueError('Too many files for the upgraded image; original unchanged.')
    result = bytearray(END*512)
    result[:SUPER*512] = release[:SUPER*512]
    result[256*512:SUPER*512] = old[256*512:SUPER*512]
    struct.pack_into('<III', result, SUPER*512, MAGIC, VER, ENTSZ)
    start = DATA
    for i, (e, payload) in enumerate(records.values()):
        name = encode_name(e['name'])
        if start + e['nsect'] > END:
            raise ValueError('Not enough space for the upgraded image; original unchanged.')
        struct.pack_into(f'<{NAMELEN}sIIHHBB2x', result, TABLE*512+i*ENTSZ,
                         name, e['size'], e['mtime'], start if e['nsect'] else 0,
                         e['nsect'], 1, e['attr'])
        result[start*512:(start+e['nsect'])*512] = payload
        start += e['nsect']
    ensure_fs(result)
    with open(output, 'xb') as target:
        target.write(result)
    print(f'Upgraded {output}: {len(records)} entries, current kernel and extensions; original unchanged.')

if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[1] == "--upgrade":
        do_upgrade(a[0], a[2])
    elif len(a) == 2 and a[1] == "--list":
        do_list(a[0])
    elif len(a) in (2, 3):
        img, src = a[0], a[1]
        dst = a[2] if len(a) == 3 else os.path.basename(src)
        do_copy(img, src, dst)
    else:
        print(__doc__)
        sys.exit(1)
