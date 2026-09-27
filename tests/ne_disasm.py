#!/usr/bin/env python
"""Takes apart a 16-bit Windows NE file and disassembles its segments.

Used to confirm findings from the port-write traces in the driver code, not
only measure them. Always use the files that really ran during the
measurement.

    python tests/ne_disasm.py <file.drv>                    # segment overview
    python tests/ne_disasm.py <file.drv> --seg 1 --at 0x021E --count 60
    python tests/ne_disasm.py <file.drv> --seg 1 --find-out  # where it writes to ports
    python tests/ne_disasm.py <file.drv> --find-imm 0x7C00   # where a constant is
"""
import argparse
import struct
import sys

import capstone


class NE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[e_lfanew:e_lfanew + 2] != b"NE":
            raise ValueError("not an NE file")
        self.ne = e_lfanew
        h = self.data[e_lfanew:]
        self.seg_table = e_lfanew + struct.unpack_from("<H", h, 0x22)[0]
        self.seg_count = struct.unpack_from("<H", h, 0x1C)[0]
        self.align = 1 << struct.unpack_from("<H", h, 0x32)[0]

    def segments(self):
        out = []
        for i in range(self.seg_count):
            off = self.seg_table + i * 8
            sector, length, flags, alloc = struct.unpack_from("<HHHH", self.data, off)
            file_off = sector * self.align
            if length == 0 and sector:
                length = 0x10000
            out.append({
                "index": i + 1,
                "file_off": file_off,
                "length": length,
                "flags": flags,
                "code": not (flags & 0x0001),   # bit0: 0 = code, 1 = data
            })
        return out

    def seg_bytes(self, index):
        s = self.segments()[index - 1]
        return self.data[s["file_off"]:s["file_off"] + s["length"]], s


def walk_func(code, start, limit=3000):
    """Disassembles a function from its prologue to its end.

    16-bit code cannot be disassembled linearly from the start of a segment,
    but from the real entry of a function the alignment is right. It ends at
    the ret/retf that follows the prologue and is not jumped over by a
    forward jump.
    """
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    out = []
    furthest_jump = start
    for ins in md.disasm(code[start:start + limit], start):
        out.append(ins)
        if ins.mnemonic.startswith("j") or ins.mnemonic == "loop":
            try:
                target = int(ins.op_str, 0)
                if target > furthest_jump:
                    furthest_jump = target
            except ValueError:
                pass
        if ins.mnemonic in ("ret", "retf") and ins.address >= furthest_jump:
            break
        if ins.mnemonic in ("jmp",) and ins.address >= furthest_jump:
            break
    return out


def disasm(code, base, start, count):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    md.detail = False
    n = 0
    for ins in md.disasm(code[start:], base + start):
        print(f"  {ins.address:04X}:  {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")
        n += 1
        if n >= count:
            break


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--seg", type=int)
    ap.add_argument("--at", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--find-out", action="store_true",
                    help="find out/in instructions to ports")
    ap.add_argument("--find-funcs", action="store_true",
                    help="find function entries (prologue push bp; mov bp,sp)")
    ap.add_argument("--funcs", action="store_true",
                    help="walk the functions from their prologue and print an overview")
    ap.add_argument("--func", type=lambda x: int(x, 0),
                    help="print the whole function from the given offset")
    ap.add_argument("--grep", help="with --funcs print only functions containing this text")
    ap.add_argument("--find-imm", type=lambda x: int(x, 0),
                    help="find occurrences of a 16-bit constant in the code")
    args = ap.parse_args()

    ne = NE(args.path)
    segs = ne.segments()

    if args.seg is None:
        print(f"{len(segs)} segments, alignment {ne.align}")
        for s in segs:
            kind = "code " if s["code"] else "data"
            print(f"  seg {s['index']:2}  {kind}  offset {s['file_off']:#08x}  "
                  f"length {s['length']:#06x}  flags {s['flags']:#06x}")
        return 0

    code, s = ne.seg_bytes(args.seg)
    print(f"segment {args.seg}: offset {s['file_off']:#x}, length {len(code):#x}, "
          f"{'code' if s['code'] else 'data'}\n")

    def func_starts():
        out = []
        pos = code.find(b"\x55\x8b\xec")
        while pos >= 0:
            out.append(pos)
            pos = code.find(b"\x55\x8b\xec", pos + 1)
        return out

    if args.func is not None:
        for ins in walk_func(code, args.func):
            print(f"  {ins.address:04X}:  {ins.bytes.hex():<14} {ins.mnemonic} {ins.op_str}")
        return 0

    if args.funcs:
        for st in func_starts():
            ins = walk_func(code, st)
            text = "\n".join(f"{i.mnemonic} {i.op_str}" for i in ins)
            if args.grep and args.grep.lower() not in text.lower():
                continue
            end = ins[-1].address if ins else st
            io_n = sum(1 for i in ins if i.mnemonic in ("out", "in"))
            print(f"  {st:#06x}..{end:#06x}  {len(ins):4} instructions"
                  f"{'   port I/O: ' + str(io_n) if io_n else ''}")
        return 0

    if args.find_funcs:
        # 16-bit code cannot be disassembled linearly, but the prologue
        # "push bp; mov bp, sp" (55 8B EC) reliably marks a function entry
        # and from it the disassembly is aligned.
        starts = []
        pos = code.find(b"\x55\x8b\xec")
        while pos >= 0:
            starts.append(pos)
            pos = code.find(b"\x55\x8b\xec", pos + 1)
        print(f"{len(starts)} function entries")
        for p in starts:
            print(f"  {p:#06x}")
        return 0

    if args.find_imm is not None:
        pat = struct.pack("<H", args.find_imm)
        hits = []
        pos = code.find(pat)
        while pos >= 0:
            hits.append(pos)
            pos = code.find(pat, pos + 1)
        print(f"constant {args.find_imm:#06x}: {len(hits)} occurrences")
        for h in hits[:20]:
            print(f"  at {h:#06x}, context:")
            disasm(code, 0, max(0, h - 12), 8)
            print()
        return 0

    if args.find_out:
        # A linear sweep of 16-bit code goes off the rails, so the bytes of the
        # out/in instructions with DX are searched directly and a window that
        # lands exactly on them is disassembled.
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        names = {0xEE: "out dx, al", 0xEF: "out dx, ax",
                 0xEC: "in al, dx", 0xED: "in ax, dx"}
        for pos, b in enumerate(code):
            if b not in names:
                continue
            # try an alignment so that the instruction starts exactly at pos
            for back in range(2, 24):
                start = pos - back
                if start < 0:
                    continue
                addrs = [i.address for i in md.disasm(code[start:pos + 1], start)]
                if pos in addrs:
                    print(f"  {pos:04X}: {names[b]}   context:")
                    disasm(code, 0, start, back + 1)
                    print()
                    break
        return 0

    disasm(code, 0, args.at, args.count)
    return 0


if __name__ == "__main__":
    sys.exit(main())
