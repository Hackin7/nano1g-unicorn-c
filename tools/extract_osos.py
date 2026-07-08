#!/usr/bin/env python3
import argparse
from pathlib import Path


def rd32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware")
    ap.add_argument("output")
    args = ap.parse_args()

    data = Path(args.firmware).read_bytes()
    if len(data) < 0x600 or rd32(data, 0x100) != 0x5B68695D:
        raise SystemExit("not a wrapped firmware image")

    dir_off = rd32(data, 0x104)
    entries = dir_off + 0x200
    for off in range(entries, min(entries + 0x2000, len(data) - 40), 40):
        dev = data[off:off + 4]
        if dev == b"\0\0\0\0":
            break
        ident = data[off + 4:off + 8][::-1]
        dev_off = rd32(data, off + 12)
        length = rd32(data, off + 16)
        if ident == b"osos":
            file_off = dev_off + 0x200
            Path(args.output).write_bytes(data[file_off:file_off + length])
            print(f"osos file_off=0x{file_off:x} len={length}")
            return
    raise SystemExit("osos entry not found")


if __name__ == "__main__":
    main()
