"""Lists (and optionally extracts) the contents of the data track of a CD image.

It handles two sector layouts and recognises them by where the `CD001` mark
of the primary volume descriptor lies (always logical sector 16):

  * **MODE1/2352** - typical for a `.bin` of CUE/BIN: 16 B sync and header,
    2048 B of data, 288 B EDC/ECC.
  * **2048 B per sector** - a plain `.iso`, data without a wrapper.

Without that detection an `.iso` looked damaged ("no ISO9660 at sector 16"),
because it was read with a step of 2352 B.

    python tests/iso_list.py cdrom/AWE32_DEMO.BIN
    python tests/iso_list.py "cdrom/Creative_AWE Install Disk.bin" --extract tests/out/iso
"""
import os
import struct
import sys

DATA_LEN = 2048          # the useful data in a sector is always 2048 B

# (sector size, data offset) - tried in this order
LAYOUTS = ((2048, 0), (2352, 16), (2336, 8))


class Iso:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.sector_size, self.data_off = self._detect()

    def _detect(self):
        for size, off in LAYOUTS:
            self.f.seek(16 * size + off + 1)
            if self.f.read(5) == b'CD001':
                return size, off
        # nothing fits - keep the original behaviour, so the error comes out at the PVD
        return 2352, 16

    def sector(self, lba):
        self.f.seek(lba * self.sector_size + self.data_off)
        return self.f.read(DATA_LEN)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])


def parse_dir(iso, lba, length, path, out, depth=0, extract=None):
    data = iso.read(lba, length)
    pos = 0
    while pos < len(data):
        rec = data[pos]
        if rec == 0:
            # the rest of the sector is padding, go to the next
            pos = (pos // DATA_LEN + 1) * DATA_LEN
            if pos >= len(data):
                break
            continue
        ext_lba = struct.unpack_from('<I', data, pos + 2)[0]
        ext_len = struct.unpack_from('<I', data, pos + 10)[0]
        flags = data[pos + 25]
        namelen = data[pos + 32]
        name = data[pos + 33:pos + 33 + namelen].decode('latin1')
        pos += rec

        if name in ('\x00', '\x01'):
            continue
        name = name.split(';')[0]
        full = path + '/' + name

        if flags & 0x02:      # directory
            if depth < 6:
                parse_dir(iso, ext_lba, ext_len, full, out, depth + 1, extract)
        else:
            out.append((full, ext_len, ext_lba))
            if extract:
                dst = os.path.join(extract, full.lstrip('/').replace('/', os.sep))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as fh:
                    fh.write(iso.read(ext_lba, ext_len))


def main():
    path = sys.argv[1]
    extract = None
    if '--extract' in sys.argv:
        extract = sys.argv[sys.argv.index('--extract') + 1]
        os.makedirs(extract, exist_ok=True)

    iso = Iso(path)
    pvd = iso.sector(16)
    if pvd[1:6] != b'CD001':
        print("no ISO9660 at sector 16 (found %r)" % pvd[1:6])
        return 1
    label = pvd[40:72].decode('latin1').strip()
    root = pvd[156:190]
    root_lba = struct.unpack_from('<I', root, 2)[0]
    root_len = struct.unpack_from('<I', root, 10)[0]
    print("image: %s   label: %s" % (os.path.basename(path), label))

    files = []
    parse_dir(iso, root_lba, root_len, '', files, extract=extract)
    files.sort()
    print("files: %d" % len(files))
    for name, size, lba in files:
        print("  %9d  %s" % (size, name))
    return 0


if __name__ == '__main__':
    sys.exit(main())
