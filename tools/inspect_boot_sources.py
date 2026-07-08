#!/usr/bin/env python3
import argparse
import hashlib
import math
import struct
import zipfile
from pathlib import Path


PATTERNS = [
    ("IsyS", b"IsyS"),
    ("SysI", b"SysI"),
    ("booting!", b"booting!"),
    ("diskmode", b"diskmode"),
    ("diskscan", b"diskscan"),
    ("softupdt", b"softupdt"),
    ("retailOS", b"retailOS"),
    ("c0debabe", struct.pack("<I", 0xC0DEBABE)),
]


def rd16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def entropy(data):
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    total = float(len(data))
    return -sum((c / total) * math.log(c / total, 2) for c in counts if c)


def fourcc(data, off):
    if off + 8 > len(data):
        return ""
    return bytes([data[off + 7], data[off + 6], data[off + 5], data[off + 4]]).decode(
        "latin1", "replace"
    )


def parse_wrapped(data, base=0):
    if base + 0x4200 > len(data) or rd32(data, base + 0x100) != 0x5B68695D:
        return []
    dir_off = rd32(data, base + 0x104)
    pos = base + dir_off + 0x200
    entries = []
    while pos + 40 <= len(data):
        if rd32(data, pos) == 0:
            break
        name = fourcc(data, pos)
        dev_off = rd32(data, pos + 12)
        length = rd32(data, pos + 16)
        addr = rd32(data, pos + 20)
        entry = rd32(data, pos + 24)
        payload = base + dev_off + 0x200
        if payload + length > len(data):
            break
        entries.append(
            {
                "name": name,
                "entry_off": pos,
                "payload_off": payload,
                "length": length,
                "addr": addr,
                "entry": entry,
            }
        )
        pos += 40
    return entries


def find_wrapped_starts(data):
    starts = []
    magic = struct.pack("<I", 0x5B68695D)
    pos = data.find(magic)
    while pos >= 0:
        if pos >= 0x100:
            start = pos - 0x100
            if parse_wrapped(data, start):
                starts.append(start)
        pos = data.find(magic, pos + 4)
    return starts


def find_all(data, pat):
    out = []
    pos = data.find(pat)
    while pos >= 0:
        out.append(pos)
        pos = data.find(pat, pos + 1)
    return out


def region_name(entries, off):
    for e in entries:
        start = e["payload_off"]
        end = start + e["length"]
        if start <= off < end:
            return "%s+0x%x" % (e["name"], off - start)
    return "outside-wrapped-payload"


def ascii_context(data, off, size=64):
    start = max(0, off - 16)
    end = min(len(data), off + size)
    chunk = data[start:end]
    text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    return "0x%x:%s" % (start, text)


def print_wrapped(label, data, base=0):
    entries = parse_wrapped(data, base)
    if not entries:
        print("%s wrapped=no" % label)
        return []
    print("%s wrapped=yes base=0x%x entries=%d" % (label, base, len(entries)))
    for i, e in enumerate(entries):
        payload = data[e["payload_off"] : e["payload_off"] + e["length"]]
        print(
            "  image[%02d] name=%s file_off=0x%x len=0x%x addr=0x%08x entry=0x%08x entropy=%.3f sha256=%s"
            % (
                i,
                e["name"],
                e["payload_off"],
                e["length"],
                e["addr"],
                e["addr"] + e["entry"],
                entropy(payload[: min(len(payload), 65536)]),
                sha256(payload)[:16],
            )
        )
    return entries


def scan_patterns(label, data, entries):
    print("%s pattern_scan" % label)
    for name, pat in PATTERNS:
        offsets = find_all(data, pat)
        if not offsets:
            print("  %s not-found" % name)
            continue
        shown = offsets[:8]
        rendered = ", ".join("0x%x:%s" % (o, region_name(entries, o)) for o in shown)
        if len(offsets) > len(shown):
            rendered += ", ..."
        print("  %s count=%d %s" % (name, len(offsets), rendered))
        for o in shown:
            if region_name(entries, o) == "outside-wrapped-payload":
                print("    context %s" % ascii_context(data, o))


def inspect_file(path):
    data = Path(path).read_bytes()
    print("file=%s size=%d sha256=%s" % (path, len(data), sha256(data)))
    entries = print_wrapped(path, data, 0)
    if not entries:
        starts = find_wrapped_starts(data)
        print("%s wrapped_starts=%s" % (path, ",".join("0x%x" % s for s in starts) or "none"))
        if starts:
            entries = print_wrapped(path, data, starts[0])
    scan_patterns(path, data, entries)
    return data


def inspect_zip(path, member, compare):
    with zipfile.ZipFile(path, "r") as zf:
        names = zf.namelist()
        print("zip=%s members=%s" % (path, ",".join(names)))
        if member not in names:
            print("zip_member_missing=%s" % member)
            return
        data = zf.read(member)
    print("zip_member=%s size=%d sha256=%s" % (member, len(data), sha256(data)))
    if compare:
        other = Path(compare).read_bytes()
        print("zip_compare=%s equal=%s" % (compare, data == other))
    entries = print_wrapped("%s:%s" % (path, member), data, 0)
    scan_patterns("%s:%s" % (path, member), data, entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip")
    ap.add_argument("--zip-member", default="Firmware-14.5.3.1")
    ap.add_argument("--compare-firmware")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()

    if args.zip:
        inspect_zip(args.zip, args.zip_member, args.compare_firmware)
    for path in args.files:
        inspect_file(path)


if __name__ == "__main__":
    main()
