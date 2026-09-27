#!/usr/bin/env python
"""Takes apart a 32-bit VxD (LE format) and disassembles its code.

The note-on logic of the Windows AWE32 driver is in `SBAWE.VXD`, not in
`SBAWE32.DRV` - see docs/re-notes/86box_comparison.md section 8. Always use
the file that really ran during the measurement.

    python tests/le_disasm.py <file.vxd>                  # objects and map
    python tests/le_disasm.py <file.vxd> --file-off 0x8E90 # what is at that place
    python tests/le_disasm.py <file.vxd> --at 0x1234 --count 60
    python tests/le_disasm.py <file.vxd> --xref 0x1234    # who addresses it
    python tests/le_disasm.py <file.vxd> --find-io        # port accesses
"""
import argparse
import struct
import sys

import capstone


class LE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.h = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[self.h:self.h + 2] != b"LE":
            raise ValueError("not an LE (VxD) file")
        u = lambda o: struct.unpack_from("<I", self.data, self.h + o)[0]
        self.page_size = u(40)
        self.last_page_size = u(44)
        self.obj_table_off = self.h + u(64)
        self.obj_count = u(68)
        self.page_map_off = self.h + u(72)
        self.data_pages_off = u(128)
        self.objects = self._objects()

    def _objects(self):
        out = []
        for i in range(self.obj_count):
            o = self.obj_table_off + i * 24
            size, base, flags, page_idx, page_cnt, _ = struct.unpack_from("<IIIIII", self.data, o)
            out.append({
                "index": i + 1, "virt_size": size, "base": base, "flags": flags,
                "first_page": page_idx, "page_count": page_cnt,
                "exec": bool(flags & 0x4), "read": bool(flags & 0x1),
                "write": bool(flags & 0x2),
            })
        return out

    def page_file_off(self, page_number):
        """1-based page number of the page map -> offset in the file."""
        o = self.page_map_off + (page_number - 1) * 4
        # LE page map: 3 bytes page number (big endian) + 1 byte flags
        b = self.data[o:o + 4]
        num = (b[0] << 16) | (b[1] << 8) | b[2]
        return self.data_pages_off + (num - 1) * self.page_size

    def obj_bytes(self, index):
        """Assembles the object image from its pages."""
        ob = self.objects[index - 1]
        buf = bytearray()
        for i in range(ob["page_count"]):
            fo = self.page_file_off(ob["first_page"] + i)
            buf += self.data[fo:fo + self.page_size]
        return bytes(buf[:ob["virt_size"]]) if ob["virt_size"] else bytes(buf)

    def file_off_to_va(self, file_off):
        """Finds the object and the virtual address for a given file offset."""
        for ob in self.objects:
            for i in range(ob["page_count"]):
                fo = self.page_file_off(ob["first_page"] + i)
                if fo <= file_off < fo + self.page_size:
                    return ob, ob["base"] + i * self.page_size + (file_off - fo)
        return None, None

    def va_to_obj_off(self, va):
        for ob in self.objects:
            if ob["base"] <= va < ob["base"] + max(ob["virt_size"],
                                                   ob["page_count"] * self.page_size):
                return ob, va - ob["base"]
        return None, None


def disasm(code, base, start, count, highlight=None):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    n = 0
    for ins in md.disasm(code[start:], base + start):
        mark = " <<<" if highlight and highlight in ins.op_str else ""
        print(f"  {ins.address:08X}:  {ins.bytes.hex():<16} {ins.mnemonic} {ins.op_str}{mark}")
        n += 1
        if n >= count:
            break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--obj", type=int, default=1)
    ap.add_argument("--at", type=lambda x: int(x, 0))
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--file-off", type=lambda x: int(x, 0))
    ap.add_argument("--xref", type=lambda x: int(x, 0))
    ap.add_argument("--find-io", action="store_true")
    args = ap.parse_args()

    le = LE(args.path)

    if args.file_off is not None:
        ob, va = le.file_off_to_va(args.file_off)
        if ob is None:
            print("the offset falls into no object")
            return 1
        print(f"offset {args.file_off:#x} -> object {ob['index']} "
              f"({'code' if ob['exec'] else 'data'}), virtual address {va:#010x}")
        return 0

    if args.at is None and not args.xref and not args.find_io:
        print(f"{le.obj_count} objects, page {le.page_size}, "
              f"data od {le.data_pages_off:#x}")
        for ob in le.objects:
            kind = []
            if ob["exec"]: kind.append("exec")
            if ob["read"]: kind.append("read")
            if ob["write"]: kind.append("write")
            print(f"  object {ob['index']}  base {ob['base']:#010x}  "
                  f"virt_size {ob['virt_size']:#x}  pages {ob['first_page']}"
                  f"..{ob['first_page']+ob['page_count']-1}  {','.join(kind)}")
        return 0

    code = le.obj_bytes(args.obj)
    ob = le.objects[args.obj - 1]

    if args.find_io:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        for ins in md.disasm(code, ob["base"]):
            if ins.mnemonic in ("out", "in"):
                print(f"  {ins.address:08X}:  {ins.mnemonic} {ins.op_str}")
        return 0

    if args.xref is not None:
        target = struct.pack("<I", args.xref)
        hits = []
        pos = code.find(target)
        while pos >= 0:
            hits.append(pos)
            pos = code.find(target, pos + 1)
        print(f"address {args.xref:#010x}: {len(hits)} references in object {args.obj}")
        for hp in hits:
            print(f"\n  reference at offset {hp:#x} (VA {ob['base']+hp:#010x}):")
            disasm(code, ob["base"], max(0, hp - 16), 8)
        return 0

    o = args.at - ob["base"] if args.at >= ob["base"] else args.at
    disasm(code, ob["base"], o, args.count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
