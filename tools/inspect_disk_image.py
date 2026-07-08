#!/usr/bin/env python3
import argparse
from pathlib import Path


WRAPPED_MAGIC = 0x5B68695D


def rd16(buf, off):
    return int.from_bytes(buf[off:off + 2], "little")


def rd32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def fourcc(raw):
    return raw[::-1].decode("latin1", errors="replace")


def print_mbr(data):
    if len(data) < 512:
        return
    sig = data[510:512]
    print("mbr_signature=%s" % sig.hex())
    if sig != b"\x55\xaa":
        return
    for idx in range(4):
        off = 0x1be + idx * 16
        entry = data[off:off + 16]
        ptype = entry[4]
        start = rd32(entry, 8)
        count = rd32(entry, 12)
        if ptype == 0 and start == 0 and count == 0:
            continue
        print(
            "mbr[%d] boot=0x%02x type=0x%02x start_lba=%d sectors=%d byte_off=0x%x byte_len=0x%x"
            % (idx, entry[0], ptype, start, count, start * 512, count * 512)
        )


def print_apm(data, base=0):
    if base + 512 > len(data):
        return
    if data[base:base + 2] != b"PM":
        return
    total = int.from_bytes(data[base + 4:base + 8], "big")
    print("apm base=0x%x entries=%d" % (base, total))
    for idx in range(min(total, 32)):
        off = base + idx * 512
        if off + 512 > len(data) or data[off:off + 2] != b"PM":
            break
        start = int.from_bytes(data[off + 8:off + 12], "big")
        count = int.from_bytes(data[off + 12:off + 16], "big")
        name = data[off + 16:off + 48].split(b"\0", 1)[0].decode("latin1", errors="replace")
        ptype = data[off + 48:off + 80].split(b"\0", 1)[0].decode("latin1", errors="replace")
        print(
            "apm[%d] name=%s type=%s start=%d blocks=%d byte_off=0x%x byte_len=0x%x"
            % (idx, name, ptype, start, count, start * 512, count * 512)
        )


def inspect_wrapper_at(data, image_start):
    magic_off = image_start + 0x100
    if image_start < 0 or magic_off + 8 > len(data) or rd32(data, magic_off) != WRAPPED_MAGIC:
        return
    dir_off = rd32(data, magic_off + 4)
    fmt = rd16(data, magic_off + 0x0a)
    entries = image_start + dir_off + 0x200
    if fmt > 16 or dir_off > 0x100000 or entries + 40 > len(data):
        return
    first_name = fourcc(data[entries + 4:entries + 8])
    if first_name not in ("osos", "rsrc", "aupd"):
        return
    print("wrapped_image start=0x%x magic=0x%x fmt=%d entries=0x%x" % (image_start, magic_off, fmt, entries))
    for idx in range(16):
        off = entries + idx * 40
        if off + 40 > len(data) or rd32(data, off) == 0:
            break
        dev_off = rd32(data, off + 12)
        length = rd32(data, off + 16)
        addr = rd32(data, off + 20)
        entry = rd32(data, off + 24)
        checksum = rd32(data, off + 28)
        print(
            "  image[%02d] name=%s file_off=0x%x len=0x%x addr=0x%08x entry=0x%08x checksum=0x%08x"
            % (idx, fourcc(data[off + 4:off + 8]), image_start + dev_off + 0x200, length, addr, entry, checksum)
        )


def scan_wrappers(data):
    magic = WRAPPED_MAGIC.to_bytes(4, "little")
    pos = 0
    found = []
    while True:
        pos = data.find(magic, pos)
        if pos < 0:
            break
        start = pos - 0x100
        if start >= 0:
            found.append(start)
        pos += 4
    if not found:
        print("wrapped_images=0")
        return
    for start in found[:16]:
        inspect_wrapper_at(data, start)
    if len(found) > 16:
        print("wrapped_images_truncated total=%d" % len(found))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    args = ap.parse_args()

    data = Path(args.image).read_bytes()
    print("path=%s size=%d" % (args.image, len(data)))
    print_mbr(data)
    print_apm(data, 512)
    scan_wrappers(data)


if __name__ == "__main__":
    main()
