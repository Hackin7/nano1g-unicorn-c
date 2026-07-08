#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


DEFAULT_VMA = 0x10000000
RESET_LITERAL_OFFSETS = {
    "reloc_text_start": 0x120,
    "reloc_text_end": 0x124,
    "reloc_pc_ref": 0x128,
    "entry_after_text_copy": 0x12C,
    "data_src_begin": 0x130,
    "data_src_end": 0x134,
    "payload_magic": 0x138,
    "reloc_data_dest": 0x13C,
}
PROBE_OFFSETS = (0x83A8, 0xB178, 0x3BB50, 0x84FC0)


def rd32(data, off):
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def be4(value):
    if value is None:
        return "none"
    raw = struct.pack(">I", value)
    return "".join(chr(b) if 32 <= b < 127 else "." for b in raw)


def ascii_bytes(data):
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


def dump_probe(data, off):
    chunk = data[off : off + 32]
    if not chunk:
        print("probe off=0x%x file_range=missing" % off)
        return
    print(
        "probe off=0x%x word=0x%08x/%s hex=%s ascii=\"%s\""
        % (off, rd32(data, off) or 0, be4(rd32(data, off)), chunk.hex(), ascii_bytes(chunk))
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--vma", type=lambda x: int(x, 0), default=DEFAULT_VMA)
    args = ap.parse_args()

    data = Path(args.image).read_bytes()
    print("image=%s size=0x%x vma=0x%08x" % (args.image, len(data), args.vma))

    literals = {}
    for name, off in RESET_LITERAL_OFFSETS.items():
        value = rd32(data, off)
        literals[name] = value
        if value is None:
            print("literal %-22s off=0x%x value=missing" % (name, off))
        else:
            print("literal %-22s off=0x%x value=0x%08x/%s" % (name, off, value, be4(value)))

    text_start = literals.get("reloc_text_start")
    text_end = literals.get("reloc_text_end")
    data_src_begin = literals.get("data_src_begin")
    data_src_end = literals.get("data_src_end")
    data_dest = literals.get("reloc_data_dest")
    payload_magic = literals.get("payload_magic")

    if text_start is not None and text_end is not None:
        print("text_copy guest=0x%08x..0x%08x bytes=0x%x" % (text_start, text_end, text_end - text_start))

    if data_src_begin is not None and data_src_end is not None and data_dest is not None:
        src_off = data_src_end - args.vma
        payload_len = 0x400000
        header_word = rd32(data, src_off)
        if header_word == payload_magic:
            header_len = rd32(data, src_off + 8)
            if header_len is not None:
                payload_len = header_len
        print(
            "data_reloc src=0x%08x..0x%08x dest=0x%08x payload_len=0x%x src_header=0x%08x/%s"
            % (
                data_src_end,
                data_src_end + payload_len,
                data_dest,
                payload_len,
                header_word or 0,
                be4(header_word),
            )
        )
        print("zero_fill guest=0x%08x..0x%08x bytes=0x%x" % (data_src_end, data_dest, data_dest - data_src_end))

    for off in PROBE_OFFSETS:
        dump_probe(data, off)


if __name__ == "__main__":
    main()
