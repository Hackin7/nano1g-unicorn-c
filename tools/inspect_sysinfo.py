#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def rd32(data, off):
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def ascii_field(data, off, size):
    raw = data[off : off + size]
    raw = raw.split(b"\0", 1)[0]
    return raw.decode("ascii", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sysinfo")
    args = ap.parse_args()

    data = Path(args.sysinfo).read_bytes()
    tag = data[:4].decode("ascii", errors="replace") if len(data) >= 4 else ""
    declared_size = rd32(data, 4)
    model_word = rd32(data, 0xE0)
    model_word2 = rd32(data, 0xE4)
    serial = ascii_field(data, 0x20, 0x20)
    board = ascii_field(data, 0x08, 0x10)

    print(
        "sysinfo file=%s size=%d tag=%s declared_size=0x%08x board=%s serial=%s model_e0=0x%08x model_e4=0x%08x"
        % (
            args.sysinfo,
            len(data),
            tag,
            declared_size if declared_size is not None else 0,
            board,
            serial,
            model_word if model_word is not None else 0,
            model_word2 if model_word2 is not None else 0,
        )
    )

    if tag != "IsyS":
        raise SystemExit("unexpected SysInfo tag")
    if declared_size != len(data):
        raise SystemExit("declared SysInfo size does not match file size")
    if model_word is None:
        raise SystemExit("SysInfo is too short for OSOS model word at +0xe0")


if __name__ == "__main__":
    main()
