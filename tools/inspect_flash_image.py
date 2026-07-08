#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


KNOWN_TYPES = {
    b"hslfksid": "diskmode",
    b"hslfgaid": "diagmode",
    b"hslfogol": "logo",
    b"hslfnacs": "diskscan",
    b"hslfscmv": "vmcs",
}


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def checksum(data):
    return sum(data) & 0xFFFFFFFF


def find_directory(data):
    hits = []
    for marker in KNOWN_TYPES:
        pos = data.find(marker)
        while pos >= 0:
            if pos + 0x28 <= len(data):
                hits.append(pos)
            pos = data.find(marker, pos + 1)
    if not hits:
        return None
    return min(hits)


def iter_entries(data, start):
    pos = start
    while pos + 0x28 <= len(data):
        raw_type = data[pos : pos + 8]
        if rd32(data, pos) == 0:
            break
        name = KNOWN_TYPES.get(raw_type, raw_type[::-1].decode("latin1", "replace"))
        yield {
            "entry_off": pos,
            "raw_type": raw_type,
            "name": name,
            "unknown1": rd32(data, pos + 0x08),
            "offset": rd32(data, pos + 0x0C),
            "length": rd32(data, pos + 0x10),
            "loadaddr": rd32(data, pos + 0x14),
            "unknown2": rd32(data, pos + 0x18),
            "checksum": rd32(data, pos + 0x1C),
            "version": rd32(data, pos + 0x20),
            "unknown3": rd32(data, pos + 0x24),
        }
        pos += 0x28


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--directory", type=lambda x: int(x, 0))
    ap.add_argument("--extract-dir")
    args = ap.parse_args()

    data = Path(args.image).read_bytes()
    start = args.directory if args.directory is not None else find_directory(data)
    if start is None:
        raise SystemExit("no known flash directory markers found")
    print("flash_image=%s size=%d directory=0x%x" % (args.image, len(data), start))

    out_dir = Path(args.extract_dir) if args.extract_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    count = 0
    for e in iter_entries(data, start):
        begin = e["offset"]
        end = begin + e["length"]
        in_range = 0 <= begin <= end <= len(data)
        actual = checksum(data[begin:end]) if in_range else 0
        print(
            "entry[%d] off=0x%x type=%s raw=%s img_off=0x%x len=0x%x load=0x%08x checksum=0x%08x actual=0x%08x version=0x%08x range=%s"
            % (
                count,
                e["entry_off"],
                e["name"],
                e["raw_type"].hex(),
                e["offset"],
                e["length"],
                e["loadaddr"],
                e["checksum"],
                actual,
                e["version"],
                "ok" if in_range else "bad",
            )
        )
        if out_dir and in_range:
            suffix = ".bin"
            path = out_dir / ("%02d-%s%s" % (count, e["name"], suffix))
            path.write_bytes(data[begin:end])
            print("  wrote %s" % path)
        count += 1

    if count == 0:
        raise SystemExit("no directory entries parsed")


if __name__ == "__main__":
    main()
