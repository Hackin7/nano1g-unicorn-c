#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path


WRAPPER_MAGIC = 0x5B68695D
FLASH_TYPES = {
    b"hslfksid": "diskmode",
    b"hslfgaid": "diagmode",
    b"hslfogol": "logo",
    b"hslfnacs": "diskscan",
    b"hslfscmv": "vmcs",
}


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def fourcc(data, off):
    return bytes([data[off + 7], data[off + 6], data[off + 5], data[off + 4]]).decode(
        "latin1", "replace"
    )


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def parse_wrapped(data, base=0):
    if base + 0x4200 > len(data) or rd32(data, base + 0x100) != WRAPPER_MAGIC:
        return []
    directory = base + rd32(data, base + 0x104) + 0x200
    entries = []
    while directory + 40 <= len(data):
        if rd32(data, directory) == 0:
            break
        dev_off = rd32(data, directory + 12)
        length = rd32(data, directory + 16)
        payload = base + dev_off + 0x200
        if payload + length > len(data):
            break
        entries.append(
            {
                "name": fourcc(data, directory),
                "payload": payload,
                "length": length,
                "load": rd32(data, directory + 20),
                "entry": rd32(data, directory + 24),
            }
        )
        directory += 40
    return entries


def find_wrapped_starts(data):
    magic = struct.pack("<I", WRAPPER_MAGIC)
    starts = []
    pos = data.find(magic)
    while pos >= 0:
        if pos >= 0x100 and parse_wrapped(data, pos - 0x100):
            starts.append(pos - 0x100)
        pos = data.find(magic, pos + 4)
    return starts


def find_flash_directory(data):
    hits = []
    for marker in FLASH_TYPES:
        pos = data.find(marker)
        while pos >= 0:
            if pos + 0x28 <= len(data):
                hits.append(pos)
            pos = data.find(marker, pos + 1)
    return min(hits) if hits else None


def is_arm_branch(word):
    return (word & 0x0F000000) == 0x0A000000


def is_arm_ldr_pc_relative(word):
    return (word & 0x0F7F0000) == 0x051F0000 and ((word >> 12) & 0xF) == 15


def looks_like_reset_vectors(data):
    if len(data) < 0x20:
        return False
    words = [rd32(data, i) for i in range(0, 0x20, 4)]
    hits = sum(1 for word in words if is_arm_branch(word) or is_arm_ldr_pc_relative(word))
    return hits >= 4


def reset_vector_hits(data):
    if len(data) < 0x20:
        return 0
    words = [rd32(data, i) for i in range(0, 0x20, 4)]
    return sum(1 for word in words if is_arm_branch(word) or is_arm_ldr_pc_relative(word))


def leading_byte_count(data, value):
    count = 0
    for byte in data:
        if byte != value:
            break
        count += 1
    return count


def flash_entries(data, directory):
    out = []
    pos = directory
    while pos + 0x28 <= len(data):
        raw_type = data[pos : pos + 8]
        if rd32(data, pos) == 0:
            break
        name = FLASH_TYPES.get(raw_type, raw_type[::-1].decode("latin1", "replace"))
        out.append((name, rd32(data, pos + 0x0C), rd32(data, pos + 0x10), rd32(data, pos + 0x14)))
        pos += 0x28
    return out


def classify(label, data):
    entries = parse_wrapped(data, 0)
    starts = []
    kind = "raw-payload"
    reason = "no wrapped firmware directory or flash directory was found"

    if entries:
        kind = "wrapped-firmware-bundle"
        reason = "contains firmware partition entries, not a reset-vector NOR dump"
    else:
        starts = find_wrapped_starts(data)
        directory = find_flash_directory(data)
        if starts:
            kind = "container-with-wrapped-firmware"
            reason = "contains embedded wrapped firmware partition(s), not reset-vector NOR bytes"
        elif directory is not None:
            kind = "flash-update-image-candidate"
            names = ",".join(e[0] for e in flash_entries(data, directory))
            reason = "contains updater-style flash directory entries: %s" % (names or "none")
        elif looks_like_reset_vectors(data):
            kind = "raw-reset-vector-candidate"
            reason = "starts with ARM-looking exception vectors; requires provenance before treating as Apple boot ROM"

    has_sysinfo_strings = b"IsyS" in data or b"SysI" in data
    apple_boot_rom = "unknown" if kind == "raw-reset-vector-candidate" else "no"
    print(
        "file=%s size=%d sha256=%s kind=%s apple_boot_rom=%s sysinfo_strings=%s reset_vector_hits=%d leading_ff=0x%x"
        % (
            label,
            len(data),
            sha256(data),
            kind,
            apple_boot_rom,
            str(has_sysinfo_strings).lower(),
            reset_vector_hits(data),
            leading_byte_count(data, 0xFF),
        )
    )
    print("  reason=%s" % reason)
    if entries:
        print("  wrapped_entries=%s" % ",".join(e["name"] for e in entries))
        for e in entries:
            print(
                "  image name=%s file_off=0x%x len=0x%x load=0x%08x entry=0x%08x"
                % (e["name"], e["payload"], e["length"], e["load"], e["load"] + e["entry"])
            )
    elif starts:
        print("  embedded_wrapped_starts=%s" % ",".join("0x%x" % s for s in starts))


def inspect_path(path):
    data = Path(path).read_bytes()
    classify(str(path), data)


def inspect_zip(path, member):
    with zipfile.ZipFile(path, "r") as zf:
        names = zf.namelist()
        print("zip=%s members=%s" % (path, ",".join(names)))
        if member == "all":
            for name in names:
                classify("%s:%s" % (path, name), zf.read(name))
            return
        if member not in names:
            raise SystemExit("zip member not found: %s" % member)
        data = zf.read(member)
    classify("%s:%s" % (path, member), data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip")
    ap.add_argument("--zip-member", default="Firmware-14.5.3.1")
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()

    if args.zip:
        inspect_zip(args.zip, args.zip_member)
    for path in args.files:
        inspect_path(path)


if __name__ == "__main__":
    main()
