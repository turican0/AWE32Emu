#!/usr/bin/env python
"""Reading and writing files in a FAT16 partition of a raw disk image.

Used to get files into the guest (86Box, Windows 95 / DOS) and to edit
AUTOEXEC.BAT without clicking in the emulator. Subdirectories work too
(e.g. `ls NETHERW/SOUND`, `get NETHERW/SOUND/MDI.INI`).

    python tests/fat16.py <image> ls
    python tests/fat16.py <image> get AUTOEXEC.BAT out.txt
    python tests/fat16.py <image> put local.mid MINUET.MID
    python tests/fat16.py <image> rm FOO.BAT
"""
import argparse
import os
import struct
import sys


class Fat16:
    def __init__(self, path, writable=False):
        self.path = path
        self.f = open(path, "r+b" if writable else "rb")
        mbr = self.f.read(512)
        if struct.unpack_from("<H", mbr, 510)[0] != 0xAA55:
            raise ValueError("MBR signature missing")

        self.part_lba = None
        for i in range(4):
            e = mbr[446 + i * 16: 446 + (i + 1) * 16]
            if e[4] in (0x04, 0x06, 0x0E):          # FAT16 variants
                self.part_lba = struct.unpack_from("<I", e, 8)[0]
                break
        if self.part_lba is None:
            raise ValueError("no FAT16 partition")

        self.f.seek(self.part_lba * 512)
        b = self.f.read(512)
        self.bps      = struct.unpack_from("<H", b, 11)[0]
        self.spc      = b[13]
        self.rsvd     = struct.unpack_from("<H", b, 14)[0]
        self.nfat     = b[16]
        self.rootent  = struct.unpack_from("<H", b, 17)[0]
        self.spf      = struct.unpack_from("<H", b, 22)[0]
        if b[54:59] != b"FAT16":
            raise ValueError("not FAT16: %r" % b[54:62])

        self.fat0_sec  = self.part_lba + self.rsvd
        self.root_sec  = self.fat0_sec + self.nfat * self.spf
        self.root_secs = (self.rootent * 32 + self.bps - 1) // self.bps
        self.data_sec  = self.root_sec + self.root_secs
        self.total_sec = struct.unpack_from("<H", b, 19)[0] or \
                         struct.unpack_from("<I", b, 32)[0]
        self.clusters  = (self.total_sec - (self.data_sec - self.part_lba)) // self.spc

        self.f.seek(self.fat0_sec * 512)
        self.fat = bytearray(self.f.read(self.spf * 512))

    # --- low level -------------------------------------------------------
    def _rd(self, sector, count):
        self.f.seek(sector * 512)
        return self.f.read(count * 512)

    def _wr(self, sector, data):
        self.f.seek(sector * 512)
        self.f.write(data)

    def fat_get(self, n):
        return struct.unpack_from("<H", self.fat, n * 2)[0]

    def fat_set(self, n, v):
        struct.pack_into("<H", self.fat, n * 2, v)

    def flush_fat(self):
        for i in range(self.nfat):
            self._wr(self.fat0_sec + i * self.spf, bytes(self.fat))

    def cluster_sector(self, n):
        return self.data_sec + (n - 2) * self.spc

    def chain(self, start):
        out = []
        n = start
        while 2 <= n < 0xFFF8 and len(out) < self.clusters + 2:
            out.append(n)
            n = self.fat_get(n)
        return out

    def free_clusters(self, count):
        out = []
        for n in range(2, self.clusters + 2):
            if self.fat_get(n) == 0:
                out.append(n)
                if len(out) == count:
                    return out
        raise RuntimeError("not enough free clusters")

    # --- directories -----------------------------------------------------
    def root(self):
        return bytearray(self._rd(self.root_sec, self.root_secs))

    def write_root(self, data):
        self._wr(self.root_sec, bytes(data))

    def read_dir(self, cluster):
        """Directory contents; cluster 0 = root."""
        if cluster == 0:
            return self.root()
        data = bytearray()
        for c in self.chain(cluster):
            data += self._rd(self.cluster_sector(c), self.spc)
        return data

    def write_dir(self, cluster, data):
        if cluster == 0:
            self.write_root(data)
            return
        clen = self.spc * 512
        for i, c in enumerate(self.chain(cluster)):
            self._wr(self.cluster_sector(c), bytes(data[i * clen:(i + 1) * clen]))

    def resolve_dir(self, path):
        """'WINDOWS/SYSTEM' -> directory cluster; empty path = root."""
        cluster = 0
        for p in [p for p in path.replace("\\", "/").split("/") if p]:
            hit = self.find(p, cluster)
            if not hit or not (hit[2] & 0x10):
                raise FileNotFoundError(f"directory {p} in '{path}'")
            cluster = hit[3]
        return cluster

    def resolve(self, path):
        """'WINDOWS/WIN.INI' -> (directory cluster, file name)."""
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        return self.resolve_dir("/".join(parts[:-1])), parts[-1]

    def entries(self, cluster=0):
        raw = self.read_dir(cluster)
        out = []
        for off in range(0, len(raw), 32):
            e = raw[off:off + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5 or e[11] == 0x0F:        # deleted / LFN
                continue
            name = e[0:8].decode("latin1").rstrip()
            ext = e[8:11].decode("latin1").rstrip()
            full = f"{name}.{ext}" if ext else name
            start = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            out.append((off, full, e[11], start, size))
        return out

    def find(self, name, cluster=0):
        name = name.upper()
        for off, full, attr, start, size in self.entries(cluster):
            if full.upper() == name:
                return off, full, attr, start, size
        return None

    def read_file(self, path):
        cluster, name = self.resolve(path)
        hit = self.find(name, cluster)
        if not hit:
            raise FileNotFoundError(path)
        _, _, _, start, size = hit
        data = b""
        for c in self.chain(start):
            data += self._rd(self.cluster_sector(c), self.spc)
            if len(data) >= size:
                break
        return data[:size]

    @staticmethod
    def short_name(name):
        name = name.upper()
        base, _, ext = name.partition(".")
        if len(base) > 8 or len(ext) > 3:
            raise ValueError(f"'{name}' is not 8.3")
        return base.ljust(8).encode("latin1") + ext.ljust(3).encode("latin1")

    def put(self, path, data):
        """Writes a file; an existing one is overwritten. The directory must exist."""
        dcluster, name = self.resolve(path)
        raw = self.short_name(name)
        clen = self.spc * 512
        need = max(1, (len(data) + clen - 1) // clen)

        root = self.read_dir(dcluster)
        off = None
        attr = 0x20                                   # archive
        hit = self.find(name, dcluster)
        if hit:
            off = hit[0]
            attr = hit[2]                             # keep hidden/system/ro
            for c in self.chain(hit[3]):              # free the old chain
                self.fat_set(c, 0)
        else:
            for o in range(0, len(root), 32):
                if root[o] in (0x00, 0xE5):
                    off = o
                    break
            if off is None:
                raise RuntimeError("directory is full")

        clusters = self.free_clusters(need)
        for i, c in enumerate(clusters):
            self.fat_set(c, 0xFFFF if i == need - 1 else clusters[i + 1])

        for i, c in enumerate(clusters):
            chunk = data[i * clen:(i + 1) * clen]
            chunk += b"\x00" * (clen - len(chunk))
            self._wr(self.cluster_sector(c), chunk)

        e = bytearray(32)
        e[0:11] = raw
        e[11] = attr
        struct.pack_into("<H", e, 22, 0)              # cas
        struct.pack_into("<H", e, 24, 0x2821)         # date (2000-01-01)
        struct.pack_into("<H", e, 26, clusters[0])
        struct.pack_into("<I", e, 28, len(data))
        root[off:off + 32] = e
        self.write_dir(dcluster, root)
        self.flush_fat()

    def rm(self, path):
        dcluster, name = self.resolve(path)
        hit = self.find(name, dcluster)
        if not hit:
            raise FileNotFoundError(path)
        off, _, _, start, _ = hit
        for c in self.chain(start):
            self.fat_set(c, 0)
        root = self.read_dir(dcluster)
        root[off] = 0xE5
        self.write_dir(dcluster, root)
        self.flush_fat()

    def close(self):
        self.f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("ls"); l.add_argument("dir", nargs="?", default="")
    g = sub.add_parser("get"); g.add_argument("name"); g.add_argument("out")
    p = sub.add_parser("put"); p.add_argument("src"); p.add_argument("name")
    r = sub.add_parser("rm");  r.add_argument("name")
    args = ap.parse_args()

    fs = Fat16(args.image, writable=args.cmd in ("put", "rm"))
    try:
        if args.cmd == "ls":
            free = sum(1 for n in range(2, fs.clusters + 2) if fs.fat_get(n) == 0)
            print(f"FAT16, cluster {fs.spc * 512} B, {fs.clusters} clusters, "
                  f"{free} free ({free * fs.spc * 512 / 1e6:.0f} MB)")
            cluster = fs.resolve_dir(args.dir)
            for _, name, attr, start, size in fs.entries(cluster):
                kind = "<DIR>" if attr & 0x10 else f"{size:>9}"
                print(f"  {name:14} {kind}  cluster {start}")
        elif args.cmd == "get":
            data = fs.read_file(args.name)
            with open(args.out, "wb") as f:
                f.write(data)
            print(f"{args.name}: {len(data)} B -> {args.out}")
        elif args.cmd == "put":
            with open(args.src, "rb") as f:
                data = f.read()
            fs.put(args.name, data)
            print(f"{args.src}: {len(data)} B -> {args.name}")
        elif args.cmd == "rm":
            fs.rm(args.name)
            print(f"deleted {args.name}")
    finally:
        fs.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
