'Rename a FLOPFS directory entry in an image.'

import struct, sys

SUPER, TABLE, TSECT, DATA, END = 288, 289, 10, 299, 2880
ENTSZ, NFILES = 40, 128

img, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
d = bytearray(open(img, "rb").read())
if len(new.encode()) > 23:
    sys.exit("new name too long (max 23 chars)")

for i in range(NFILES):
    off = TABLE * 512 + i * ENTSZ
    name = bytes(d[off:off + 24]).split(b"\0")[0].decode("latin1")
    used = d[off + 36]
    if used and name == old:
        d[off:off + 24] = new.encode().ljust(24, b"\0")
        open(img, "wb").write(d)
        print("renamed %s -> %s" % (old, new))
        break
else:
    sys.exit("no such file: %s" % old)
