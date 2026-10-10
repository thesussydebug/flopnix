from pathlib import Path
import struct
import sys
import zlib

HEAD = bytes([0x1f, 0x8b, 8, 4, 0, 0, 0, 0, 2, 255, 12, 0, 70, 88, 8, 0])
SIZE = len(HEAD) + 8
SLACK = 24
ON_DEMAND = 2


def packed(data):
    return data[:4] == HEAD[:4] and data[10:16] == HEAD[10:16]


def unpack(data):
    return zlib.decompress(data, 31) if packed(data) else data


def header(raw):
    shoff, = struct.unpack_from('<I', raw, 32)
    entry, count = struct.unpack_from('<HH', raw, 46)
    sections = [struct.unpack_from('<10I', raw, shoff + i * entry) for i in range(count)]
    for table in sections:
        if table[1] != 2:
            continue
        names = sections[table[6]][4]
        for at in range(table[4], table[4] + table[5], 16):
            name, value, _, _, _, index = struct.unpack_from('<IIIBBH', raw, at)
            text = raw[names + name:raw.index(b'\0', names + name)]
            if text == b'kext_header' and 0 < index < count:
                return struct.unpack_from('<IHBB16s', raw, sections[index][4] + value)
    raise ValueError('no kext_header symbol')


def boot_kind(raw):
    _, _, kind, flags, _ = header(raw)
    return 0 if flags & ON_DEMAND else kind


def margin(blob, full):
    inflate = zlib.decompressobj(31)
    produced = ahead = 0
    for at in range(len(blob)):
        produced += len(inflate.decompress(blob[at:at + 1]))
        ahead = max(ahead, produced - at - 1)
    return max(0, ahead + len(blob) - full + 1)


def pack(raw):
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    blob = (HEAD + struct.pack('<IB3x', len(raw), boot_kind(raw)) + deflate.compress(raw) + deflate.flush() +
            struct.pack('<II', zlib.crc32(raw), len(raw)))
    return blob if len(blob) < len(raw) and margin(blob, len(raw)) <= SLACK else raw


if __name__ == '__main__':
    for name in sys.argv[1:]:
        path = Path(name)
        path.write_bytes(pack(unpack(path.read_bytes())))
