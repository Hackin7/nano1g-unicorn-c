#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def is_push(word):
    return (word & 0xFFFF0000) in (0xE92D0000, 0xE52D0000)


def arm_imm12(insn):
    imm8 = insn & 0xFF
    rot = ((insn >> 8) & 0xF) * 2
    return ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF if rot else imm8


def find_function_start(data, off):
    pos = off & ~3
    floor = max(0, pos - 0x400)
    while pos >= floor:
        word = rd32(data, pos)
        if is_push(word):
            return pos
        pos -= 4
    return None


def find_raw_refs(data, packed):
    aligned = []
    unaligned = 0
    pos = data.find(packed)
    while pos >= 0:
        if (pos & 3) == 0:
            aligned.append(pos)
        else:
            unaligned += 1
        pos = data.find(packed, pos + 1)
    return aligned, unaligned


def find_arm_refs(data, vma, target_addr):
    refs = []
    for off in range(0, len(data) - 4, 4):
        insn = rd32(data, off)
        pc = (vma + off + 8) & 0xFFFFFFFF

        # ARM literal load: ldr Rt, [pc, +/-#imm12]
        if (insn & 0x0E500000) == 0x04100000 and ((insn >> 16) & 0xF) == 15:
            imm = insn & 0xFFF
            lit_addr = (pc + imm) & 0xFFFFFFFF if (insn & (1 << 23)) else (pc - imm) & 0xFFFFFFFF
            lit_off = lit_addr - vma
            if 0 <= lit_off <= len(data) - 4 and rd32(data, lit_off) == target_addr:
                refs.append(("ldr-lit", off, find_function_start(data, off), lit_addr))

        # ARM adr pseudo-instruction: add/sub Rd, pc, #imm.
        if (insn & 0x0F1F0000) in (0x028F0000, 0x024F0000):
            imm = arm_imm12(insn)
            addr = (pc + imm) & 0xFFFFFFFF if (insn & 0x00400000) else (pc - imm) & 0xFFFFFFFF
            if addr == target_addr:
                refs.append(("adr", off, find_function_start(data, off), addr))
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--vma", type=lambda x: int(x, 0), default=0x10000000)
    ap.add_argument("--needle", action="append", default=[])
    args = ap.parse_args()

    data = Path(args.image).read_bytes()
    needles = args.needle or ["booting!", "IsyS", "diskmode", "retailOS"]
    print("image=%s size=0x%x vma=0x%08x" % (args.image, len(data), args.vma))

    for needle in needles:
        nb = needle.encode("latin1")
        off = data.find(nb)
        while off >= 0:
            addr = args.vma + off
            packed = struct.pack("<I", addr)
            raw_aligned, raw_unaligned = find_raw_refs(data, packed)
            refs = [("raw32", ref, find_function_start(data, ref), addr) for ref in raw_aligned]
            refs.extend(find_arm_refs(data, args.vma, addr))
            print(
                "needle=%s off=0x%x addr=0x%08x refs=%d raw_unaligned=%d"
                % (needle, off, addr, len(refs), raw_unaligned)
            )
            for kind, ref, fn, target in refs[:24]:
                if fn is None:
                    print(
                        "  kind=%s ref_off=0x%x ref_addr=0x%08x fn=unknown target=0x%08x"
                        % (kind, ref, args.vma + ref, target)
                    )
                else:
                    print(
                        "  kind=%s ref_off=0x%x ref_addr=0x%08x fn_off=0x%x fn_addr=0x%08x target=0x%08x"
                        % (kind, ref, args.vma + ref, fn, args.vma + fn, target)
                    )
            off = data.find(nb, off + 1)


if __name__ == "__main__":
    main()
