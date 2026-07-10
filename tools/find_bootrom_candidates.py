#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path


WRAPPER_MAGIC = 0x5B68695D
SKIP_DIRS = {".git", ".deps", "build", "build-mingw", "build-msvc", "tmp"}


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fourcc(data, off):
    if off + 8 > len(data):
        return ""
    return bytes([data[off + 7], data[off + 6], data[off + 5], data[off + 4]]).decode(
        "latin1", "replace"
    )


def parse_wrapped(data, base=0):
    if base < 0 or base + 0x4200 > len(data) or rd32(data, base + 0x100) != WRAPPER_MAGIC:
        return []
    directory = base + rd32(data, base + 0x104) + 0x200
    entries = []
    for _ in range(32):
        if directory + 40 > len(data) or rd32(data, directory) == 0:
            break
        dev_off = rd32(data, directory + 12)
        length = rd32(data, directory + 16)
        payload = base + dev_off + 0x200
        if payload + length > len(data):
            break
        entries.append(fourcc(data, directory))
        directory += 40
    return entries


def find_wrapped_starts(data):
    starts = []
    magic = struct.pack("<I", WRAPPER_MAGIC)
    pos = data.find(magic)
    while pos >= 0:
        start = pos - 0x100
        if start >= 0 and parse_wrapped(data, start):
            starts.append(start)
        pos = data.find(magic, pos + 4)
    return starts


def is_arm_branch(word):
    return (word & 0x0F000000) == 0x0A000000


def is_arm_ldr_pc_relative(word):
    return (word & 0x0F7F0000) == 0x051F0000 and ((word >> 12) & 0xF) == 15


def vector_hits_at_start(data):
    if len(data) < 0x20:
        return 0
    words = [rd32(data, off) for off in range(0, 0x20, 4)]
    return sum(1 for word in words if is_arm_branch(word) or is_arm_ldr_pc_relative(word))


def has_printable_ratio(data):
    if not data:
        return 0.0
    sample = data[: min(len(data), 4096)]
    printable = sum(1 for b in sample if b in (9, 10, 13) or 32 <= b < 127)
    return printable / len(sample)


def classify_blob(label, data, min_vector_hits, max_candidate_printable_ratio):
    entries = parse_wrapped(data, 0)
    if entries:
        print(
            "file=%s kind=wrapped-firmware-bundle entries=%s sha256=%s"
            % (label, ",".join(entries), sha256(data)[:16])
        )
        return "wrapped"

    starts = find_wrapped_starts(data)
    if starts:
        print(
            "file=%s kind=container-with-wrapped-firmware embedded_wrapped_starts=%s sha256=%s"
            % (label, ",".join("0x%x" % s for s in starts), sha256(data)[:16])
        )
        return "container"

    hits = vector_hits_at_start(data)
    printable_ratio = has_printable_ratio(data)
    if hits >= min_vector_hits and printable_ratio <= max_candidate_printable_ratio:
        print(
            "file=%s kind=raw-reset-vector-candidate vector_hits=%u printable_ratio=%.3f sha256=%s"
            % (label, hits, printable_ratio, sha256(data)[:16])
        )
        return "candidate"

    return "other"


def should_skip(path):
    return any(part in SKIP_DIRS for part in path.parts)


def iter_files(roots):
    for root in roots:
        path = Path(root)
        if path.is_file():
            yield path
            continue
        if not path.exists():
            continue
        for child in path.rglob("*"):
            if child.is_file() and not should_skip(child):
                yield child


def scan_zip(path, min_vector_hits, max_candidate_printable_ratio):
    counts = {"wrapped": 0, "container": 0, "candidate": 0, "other": 0}
    with zipfile.ZipFile(path, "r") as zf:
        print("zip=%s members=%s" % (path, ",".join(zf.namelist())))
        for name in zf.namelist():
            data = zf.read(name)
            kind = classify_blob(
                "%s:%s" % (path, name),
                data,
                min_vector_hits,
                max_candidate_printable_ratio,
            )
            counts[kind] += 1
    return counts


def add_counts(total, counts):
    for key, value in counts.items():
        total[key] = total.get(key, 0) + value


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+", help="files or directories to scan")
    ap.add_argument("--min-vector-hits", type=int, default=4)
    ap.add_argument("--max-candidate-printable-ratio", type=float, default=0.70)
    ap.add_argument("--max-size", type=int, default=256 * 1024 * 1024)
    args = ap.parse_args()

    total = {"wrapped": 0, "container": 0, "candidate": 0, "other": 0, "skipped": 0}
    for path in iter_files(args.roots):
        if path.stat().st_size > args.max_size:
            total["skipped"] += 1
            print("file=%s kind=skipped-too-large size=%d" % (path, path.stat().st_size))
            continue
        if path.suffix.lower() == ".zip":
            add_counts(
                total,
                scan_zip(path, args.min_vector_hits, args.max_candidate_printable_ratio),
            )
            continue
        data = path.read_bytes()
        kind = classify_blob(
            str(path),
            data,
            args.min_vector_hits,
            args.max_candidate_printable_ratio,
        )
        total[kind] += 1

    print(
        "summary wrapped=%u containers=%u reset_candidates=%u other=%u skipped=%u"
        % (total["wrapped"], total["container"], total["candidate"], total["other"], total["skipped"])
    )


if __name__ == "__main__":
    main()
