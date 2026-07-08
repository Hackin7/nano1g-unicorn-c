#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


FWUP_MAGIC = 0x46775570
END_MAGIC = 0x456E6421
RECORD_SIZE = 28
MAX_RECORDS = 8

KEY_NAMES = {
    0x436D6446: "CmdF/iPod Updater Batch Command File",
    0x54333047: "T30G/Toshiba MK3004MAH HDD firmware",
    0x54314C47: "T1LG/Toshiba MK1003GAL HDD firmware",
    0x54354D4C: "T5ML/Toshiba MK5002MAL HDD firmware",
    0x466C7368: "Flsh/iPod CFI flash firmware",
    0x666C7368: "flsh/iPod CFI flash firmware",
}


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def fourcc_be(value):
    raw = struct.pack(">I", value)
    return "".join(chr(b) if 32 <= b < 127 else "." for b in raw)


def record_at(data, off):
    if off + RECORD_SIZE > len(data):
        return None
    return {
        "magic": rd32(data, off + 0),
        "offset": rd32(data, off + 4),
        "key": rd32(data, off + 8),
        "size": rd32(data, off + 12),
        "arg0": rd32(data, off + 16),
        "arg1": rd32(data, off + 20),
        "checksum": rd32(data, off + 24),
    }


def iter_magic_offsets(data):
    needle = struct.pack("<I", FWUP_MAGIC)
    pos = data.find(needle)
    while pos >= 0:
        yield pos
        pos = data.find(needle, pos + 1)


def parse_stream(data, off):
    records = []
    pos = off
    for _ in range(MAX_RECORDS):
        rec = record_at(data, pos)
        if rec is None or rec["magic"] != FWUP_MAGIC:
            break
        records.append(rec)
        pos += RECORD_SIZE
    end = rd32(data, pos) if pos + 4 <= len(data) else None
    return records, end, pos


def print_record(index, rec):
    key_name = KEY_NAMES.get(rec["key"], "unknown")
    print(
        "    rec[%d] key=0x%08x/%s %s offset=0x%x size=0x%x arg0=0x%x arg1=0x%x checksum=0x%08x"
        % (
            index,
            rec["key"],
            fourcc_be(rec["key"]),
            key_name,
            rec["offset"],
            rec["size"],
            rec["arg0"],
            rec["arg1"],
            rec["checksum"],
        )
    )


def inspect(path, min_records, valid_only):
    data = Path(path).read_bytes()
    print("file=%s size=%d" % (path, len(data)))
    streams = 0
    valid_streams = 0
    magic_hits = 0
    for off in iter_magic_offsets(data):
        magic_hits += 1
        records, end, end_off = parse_stream(data, off)
        if len(records) < min_records:
            continue
        valid = end == END_MAGIC
        if valid:
            valid_streams += 1
        if valid_only and not valid:
            continue
        streams += 1
        end_text = "none" if end is None else "0x%08x/%s" % (end, fourcc_be(end))
        print(
            "  stream off=0x%x records=%d end_off=0x%x end=%s valid_end=%s"
            % (off, len(records), end_off, end_text, str(valid).lower())
        )
        for i, rec in enumerate(records):
            print_record(i, rec)
    print("  magic_hits=%d shown_streams=%d valid_streams=%d" % (magic_hits, streams, valid_streams))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-records", type=int, default=1)
    ap.add_argument("--valid-only", action="store_true")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    for path in args.files:
        inspect(path, args.min_records, args.valid_only)


if __name__ == "__main__":
    main()
