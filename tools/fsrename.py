'Rename a FLOPFS directory entry in an image.'

import sys
from fscp import TABLE, ENTSZ, load, ensure_fs, entries, encode_name

img, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
encoded = encode_name(new)
d = load(img)
ensure_fs(d)
live = [e for e in entries(d) if e['used']]
if old != new and any(e['name'] == new for e in live):
    sys.exit("destination already exists: %s" % new)
for e in live:
    if e['name'] == old:
        off = TABLE * 512 + e['i'] * ENTSZ
        d[off:off + 24] = encoded.ljust(24, b"\0")
        with open(img, "wb") as target:
            target.write(d)
        print("renamed %s -> %s" % (old, new))
        break
else:
    sys.exit("no such file: %s" % old)
