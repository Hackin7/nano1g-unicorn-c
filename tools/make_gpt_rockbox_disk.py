#!/usr/bin/env python3
import argparse
import shutil
import struct
import uuid
import zlib
from pathlib import Path


BASIC_DATA = uuid.UUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7").bytes_le
PARTITION_GUID = uuid.UUID("4C2A89E8-70AA-469A-8B0C-8B6C5D2D4F11").bytes_le


def u32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def find_fat_partition(data):
    if len(data) < 512 or data[510:512] != b"\x55\xaa":
        raise SystemExit("source disk has no MBR signature")

    for idx in range(4):
        off = 0x1BE + idx * 16
        entry = data[off:off + 16]
        ptype = entry[4]
        start = u32(entry, 8)
        count = u32(entry, 12)
        if ptype in (0x0B, 0x0C) and count:
            return start, count

    raise SystemExit("source disk has no FAT32 MBR partition")


def write_protective_mbr(data, total_lba):
    mbr = bytearray(512)
    mbr[0x1BE + 4] = 0xEE
    mbr[0x1BE + 8:0x1BE + 12] = (1).to_bytes(4, "little")
    mbr_count = min(total_lba - 1, 0xFFFFFFFF)
    mbr[0x1BE + 12:0x1BE + 16] = mbr_count.to_bytes(4, "little")
    mbr[510:512] = b"\x55\xaa"
    data[0:512] = mbr


def build_entries(fat_start, fat_count):
    entries = bytearray(128 * 128)
    first_lba = fat_start
    last_lba = fat_start + fat_count - 1
    entry = bytearray(128)
    entry[0:16] = BASIC_DATA
    entry[16:32] = PARTITION_GUID
    entry[32:40] = first_lba.to_bytes(8, "little")
    entry[40:48] = last_lba.to_bytes(8, "little")
    entry[56:56 + len("Rockbox".encode("utf-16le"))] = "Rockbox".encode("utf-16le")
    entries[0:128] = entry
    return entries


def build_header(total_lba, entries):
    header = bytearray(512)
    header[0:8] = b"EFI PART"
    struct.pack_into("<IIII", header, 8, 0x00010000, 92, 0, 0)
    struct.pack_into("<QQQQ", header, 24, 1, total_lba - 1, 34, total_lba - 34)
    disk_guid = uuid.UUID("2E5D6D98-21AF-46F9-BAA4-44D90D9E0C21").bytes_le
    header[56:72] = disk_guid
    struct.pack_into("<QIII", header, 72, 2, 128, 128, zlib.crc32(entries) & 0xFFFFFFFF)

    crc = zlib.crc32(header[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", header, 16, crc)
    return header


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source")
    ap.add_argument("output")
    args = ap.parse_args()

    src = Path(args.source)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(str(src), str(out))

    data = bytearray(out.read_bytes())
    if len(data) % 512:
        raise SystemExit("disk size is not sector aligned")
    total_lba = len(data) // 512
    fat_start, fat_count = find_fat_partition(data)

    write_protective_mbr(data, total_lba)
    entries = build_entries(fat_start, fat_count)
    data[512:1024] = build_header(total_lba, entries)
    data[1024:1024 + len(entries)] = entries
    out.write_bytes(data)
    print(f"wrote {out} fat_start={fat_start} fat_count={fat_count}")


if __name__ == "__main__":
    main()
