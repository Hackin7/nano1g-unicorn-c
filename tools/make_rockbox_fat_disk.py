#!/usr/bin/env python3
"""Build an MBR disk image whose FAT32 partition contains a Rockbox tree.

The result is the source image expected by tools/make_gpt_rockbox_disk.py.
Rockbox 4.0 mounts that GPT (it treats protective type 0xEE as a GPT disk)
and also mounts this MBR directly. The FAT partition starts at LBA 2048 so
the GPT entry array, which occupies LBAs 2..33, does not overlap it, and so
Rockbox's GPT parser accepts the start (it rejects starts below LBA 34).

No mkfs.vfat or mtools dependency: the image is produced entirely here.
"""

import argparse
import os
import struct
import sys
from pathlib import Path


SECTOR = 512
PART_START = 2048
RESERVED = 32
MEDIA = 0xF8
# 2025-03-31 15:33:00, the timestamp inside the Rockbox 4.0 nano zip.
DOS_TIME = 0x7C20
DOS_DATE = 0x5A7F
VOLUME_LABEL = "ROCKBOX"
VOLUME_ID = 0x52424F58  # 'RBOX'


def dos_name_checksum(short):
    total = 0
    for byte in short:
        total = (((total & 1) << 7) | (total >> 1)) + byte
        total &= 0xFF
    return total


def sanitize_8p3(text):
    out = []
    for ch in text.upper():
        if ch in ' "*+,./:;<=>?[]|' or ord(ch) < 0x20 or ord(ch) > 0x7E:
            out.append("_")
        else:
            out.append(ch)
    return "".join(out)


def make_short(name, used):
    if name in (".", ".."):
        base = name
        ext = ""
        token = (base.ljust(8) + ext.ljust(3)).encode("ascii")
        return token

    if name.startswith(".") and name.count(".") == 1:
        base, ext = name[1:], ""
    elif "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    base_s = sanitize_8p3(base)[:8]
    ext_s = sanitize_8p3(ext)[:3]
    exact = (
        base_s == base.upper()
        and ext_s == ext.upper()
        and len(base) <= 8
        and len(ext) <= 3
        and name == name.upper()
        and not name.startswith(".")
        and name.count(".") <= 1
        and base_s != ""
    )
    if exact:
        token = (base_s.ljust(8) + ext_s.ljust(3)).encode("ascii")
        if token not in used:
            used.add(token)
            return token

    stem = sanitize_8p3(base)[:6] or "FILE"
    number = 1
    while True:
        tail = "~%d" % number
        prefix = stem[: 8 - len(tail)]
        token = ((prefix + tail).ljust(8) + ext_s.ljust(3)).encode("ascii")
        if token not in used:
            used.add(token)
            return token
        number += 1


def lfn_entries(name, checksum):
    encoded = name.encode("utf-16le")
    chars = encoded + b"\x00\x00"
    # Pad to a multiple of 13 UTF-16 code units with 0xFFFF after the NUL.
    units = 13 * ((len(chars) // 2 + 12) // 13)
    chars = chars + b"\xff\xff" * (units - len(chars) // 2)
    count = len(chars) // 26
    entries = []
    for index in range(count, 0, -1):
        piece = chars[(index - 1) * 26 : index * 26]
        seq = index | (0x40 if index == count else 0)
        raw = bytearray(32)
        raw[0] = seq
        raw[1:11] = piece[0:10]
        raw[11] = 0x0F
        raw[12] = 0x00
        raw[13] = checksum
        raw[14:26] = piece[10:22]
        raw[26:28] = b"\x00\x00"
        raw[28:32] = piece[22:26]
        entries.append(bytes(raw))
    return entries


def short_entry(short, attr, cluster, size):
    raw = bytearray(32)
    raw[0:11] = short
    raw[11] = attr
    struct.pack_into("<H", raw, 14, DOS_TIME)
    struct.pack_into("<H", raw, 16, DOS_DATE)
    struct.pack_into("<H", raw, 18, DOS_DATE)
    struct.pack_into("<H", raw, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", raw, 22, DOS_TIME)
    struct.pack_into("<H", raw, 24, DOS_DATE)
    struct.pack_into("<H", raw, 26, cluster & 0xFFFF)
    struct.pack_into("<I", raw, 28, size)
    return bytes(raw)


class Node(object):
    def __init__(self, name, is_dir, data=b""):
        self.name = name
        self.is_dir = is_dir
        self.data = data
        self.children = []
        self.cluster = 0
        self.short = b""
        self.chain = []


def scan_tree(path):
    node = Node(path.name, True)
    for child in sorted(path.iterdir(), key=lambda item: item.name):
        if child.is_dir():
            node.children.append(scan_tree(child))
        elif child.is_file():
            node.children.append(Node(child.name, False, child.read_bytes()))
    return node


def assign_short_names(node, used):
    for child in node.children:
        child.short = make_short(child.name, used)
        if child.is_dir:
            assign_short_names(child, set())


def lfn_count(name):
    units = len(name.encode("utf-16le")) // 2 + 1  # include the terminating NUL
    return (units + 12) // 13


def directory_entry_bytes(node, cluster_size):
    count = 2  # '.' and '..'
    for child in node.children:
        count += 1
        if child.name not in (".", ".."):
            count += lfn_count(child.name)
    span = ((count * 32 + cluster_size - 1) // cluster_size) * cluster_size
    return max(span, cluster_size)


def directory_bytes(node, cluster_size):
    blob = bytearray()
    blob += short_entry(b".          ", 0x10, node.cluster, 0)
    parent = 0 if node.parent_cluster is None else node.parent_cluster
    blob += short_entry(b"..         ", 0x10, parent, 0)
    for child in node.children:
        if child.name not in (".", ".."):
            blob += b"".join(lfn_entries(child.name, dos_name_checksum(child.short)))
        attr = 0x10 if child.is_dir else 0x20
        size = 0 if child.is_dir else len(child.data)
        blob += short_entry(child.short, attr, child.cluster, size)
    span = ((len(blob) + cluster_size - 1) // cluster_size) * cluster_size
    blob += b"\x00" * (span - len(blob))
    if not blob:
        blob = b"\x00" * cluster_size
    return bytes(blob)


def allocate(node, alloc, parent_cluster):
    node.parent_cluster = parent_cluster
    # Entry count depends only on names, so the directory cluster can be
    # reserved before children are placed. Subdirectory '..' then points at it.
    count = directory_entry_bytes(node, alloc.cluster_size) // alloc.cluster_size
    node.cluster = alloc.take(count)
    node.chain = list(alloc.last)
    for child in node.children:
        if child.is_dir:
            allocate(child, alloc, node.cluster)
        elif child.data:
            file_count = (len(child.data) + alloc.cluster_size - 1) // alloc.cluster_size
            child.cluster = alloc.take(file_count)
            child.chain = list(alloc.last)
        else:
            child.cluster = 0
            child.chain = []
    node.payload = directory_bytes(node, alloc.cluster_size)
    if len(node.payload) != count * alloc.cluster_size:
        raise SystemExit("directory cluster count changed for %s" % node.name)


class Allocator(object):
    def __init__(self, cluster_size):
        self.cluster_size = cluster_size
        self.next = 2
        self.last = []

    @property
    def peek(self):
        return self.next

    def take(self, count):
        start = self.next
        self.last = list(range(start, start + count))
        self.next += count
        return start


def fat_geometry(part_sectors):
    fat_sectors = 1
    while True:
        data_sectors = part_sectors - RESERVED - 2 * fat_sectors
        if data_sectors < 1:
            raise SystemExit("partition is too small for FAT32")
        clusters = data_sectors  # 1 sector per cluster
        need = ((clusters + 2) * 4 + SECTOR - 1) // SECTOR
        if need == fat_sectors:
            return fat_sectors, clusters
        fat_sectors = need


def build_image(root, part_sectors):
    cluster_size = SECTOR
    fat_sectors, cluster_capacity = fat_geometry(part_sectors)
    alloc = Allocator(cluster_size)
    assign_short_names(root, set())
    allocate(root, alloc, None)
    used_clusters = alloc.next - 2
    if used_clusters > cluster_capacity:
        raise SystemExit(
            "Rockbox tree needs %d clusters but the partition holds %d"
            % (used_clusters, cluster_capacity)
        )

    image = bytearray((PART_START + part_sectors) * SECTOR)
    write_mbr(image, part_sectors)

    part_off = PART_START * SECTOR
    write_bpb(image, part_off, part_sectors, fat_sectors, cluster_capacity - used_clusters)
    fat = bytearray(fat_sectors * SECTOR)
    put_fat(fat, 0, 0x0FFFFFF8)
    put_fat(fat, 1, 0x0FFFFFFF)

    def write_chain(chain, payload):
        for index, cluster in enumerate(chain):
            put_fat(fat, cluster, 0x0FFFFFFF if index + 1 == len(chain) else chain[index + 1])
            dest = data_offset(cluster, fat_sectors)
            chunk = payload[index * cluster_size : (index + 1) * cluster_size]
            image[part_off + dest : part_off + dest + len(chunk)] = chunk

    def walk(node):
        if node.is_dir:
            write_chain(node.chain, node.payload)
            for child in node.children:
                walk(child)
        elif node.chain:
            write_chain(node.chain, node.data)

    walk(root)
    fat_off = part_off + RESERVED * SECTOR
    image[fat_off : fat_off + len(fat)] = fat
    image[fat_off + len(fat) : fat_off + 2 * len(fat)] = fat
    return bytes(image), used_clusters, cluster_capacity


def data_offset(cluster, fat_sectors):
    return (RESERVED + 2 * fat_sectors + (cluster - 2)) * SECTOR


def put_fat(fat, index, value):
    off = index * 4
    previous = int.from_bytes(fat[off : off + 4], "little")
    merged = (previous & 0xF0000000) | (value & 0x0FFFFFFF)
    fat[off : off + 4] = struct.pack("<I", merged)


def write_mbr(image, part_sectors):
    # A type-0 slot occupies the classic firmware gap. Rockbox skips type 0
    # and mounts the following FAT32 partition. make_gpt_rockbox_disk.py
    # finds that FAT partition and points the GPT entry at the same LBAs.
    fw_start = 63
    fw_count = PART_START - fw_start
    entry = 0x1BE
    struct.pack_into("<I", image, entry + 8, fw_start)
    struct.pack_into("<I", image, entry + 12, fw_count)
    entry = 0x1CE
    image[entry + 4] = 0x0C
    struct.pack_into("<I", image, entry + 8, PART_START)
    struct.pack_into("<I", image, entry + 12, part_sectors)
    image[510] = 0x55
    image[511] = 0xAA


def write_bpb(image, part_off, part_sectors, fat_sectors, free_clusters):
    bpb = bytearray(SECTOR)
    bpb[0:3] = b"\xEB\x58\x90"
    bpb[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", bpb, 11, SECTOR)
    bpb[13] = 1
    struct.pack_into("<H", bpb, 14, RESERVED)
    bpb[16] = 2
    bpb[21] = MEDIA
    struct.pack_into("<H", bpb, 24, 63)
    struct.pack_into("<H", bpb, 26, 255)
    struct.pack_into("<I", bpb, 28, PART_START)
    struct.pack_into("<I", bpb, 32, part_sectors)
    struct.pack_into("<I", bpb, 36, fat_sectors)
    struct.pack_into("<I", bpb, 44, 2)  # root cluster
    struct.pack_into("<H", bpb, 48, 1)  # FSInfo sector
    struct.pack_into("<H", bpb, 50, 6)  # backup boot sector
    bpb[64] = 0x80
    bpb[66] = 0x29
    struct.pack_into("<I", bpb, 67, VOLUME_ID)
    label = VOLUME_LABEL.encode("ascii").ljust(11)
    bpb[71:82] = label
    bpb[82:90] = b"FAT32   "
    bpb[510] = 0x55
    bpb[511] = 0xAA
    image[part_off : part_off + SECTOR] = bpb
    image[part_off + 6 * SECTOR : part_off + 7 * SECTOR] = bpb

    info = bytearray(SECTOR)
    struct.pack_into("<I", info, 0, 0x41615252)
    struct.pack_into("<I", info, 484, 0x61417272)
    struct.pack_into("<I", info, 488, free_clusters)
    struct.pack_into("<I", info, 492, 2)
    info[510] = 0x55
    info[511] = 0xAA
    image[part_off + SECTOR : part_off + 2 * SECTOR] = info


def read_tree(image):
    """Return {posix path: bytes} for files in a disk this module built."""
    if image[510:512] != b"\x55\xAA":
        raise SystemExit("missing MBR signature")
    start = struct.unpack_from("<I", image, 0x1CE + 8)[0]
    count = struct.unpack_from("<I", image, 0x1CE + 12)[0]
    part = image[start * SECTOR : (start + count) * SECTOR]
    if part[510:512] != b"\x55\xAA" or part[82:90] != b"FAT32   ":
        raise SystemExit("partition is not the FAT32 we wrote")
    reserved = struct.unpack_from("<H", part, 14)[0]
    fat_sectors = struct.unpack_from("<I", part, 36)[0]
    fat = part[reserved * SECTOR : (reserved + fat_sectors) * SECTOR]

    def next_cluster(cluster):
        value = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
        if value >= 0x0FFFFFF8:
            return None
        return value

    def read_chain(cluster):
        blob = bytearray()
        seen = set()
        while cluster and cluster not in seen:
            seen.add(cluster)
            off = data_offset(cluster, fat_sectors)
            blob += part[off : off + SECTOR]
            cluster = next_cluster(cluster)
        return bytes(blob)

    def decode_dir(blob):
        files = []
        pending = []
        for index in range(0, len(blob), 32):
            ent = blob[index : index + 32]
            if ent[0] == 0x00:
                break
            if ent[0] == 0xE5:
                pending = []
                continue
            if ent[11] == 0x0F:
                pending.append(ent)
                continue
            name = ent[0:8].decode("ascii").rstrip(" ")
            ext = ent[8:11].decode("ascii").rstrip(" ")
            short = name + (("." + ext) if ext else "")
            if pending:
                chars = bytearray()
                for piece in reversed(pending):
                    chars += piece[1:11] + piece[14:26] + piece[28:32]
                text = chars.decode("utf-16le").split("\x00", 1)[0]
                pending = []
            else:
                text = short
            cluster = struct.unpack_from("<H", ent, 26)[0] | (
                struct.unpack_from("<H", ent, 20)[0] << 16
            )
            size = struct.unpack_from("<I", ent, 28)[0]
            files.append((text, bool(ent[11] & 0x10), cluster, size))
        return files

    found = {}

    def walk(cluster, prefix):
        for name, is_dir, first, size in decode_dir(read_chain(cluster)):
            if name in (".", ".."):
                continue
            path = prefix + name
            if is_dir:
                walk(first, path + "/")
            else:
                data = read_chain(first)[:size] if first else b""
                found[path] = data

    walk(2, "")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="directory whose contents become the FAT root")
    parser.add_argument("output", type=Path)
    parser.add_argument("--mib", type=int, default=96, help="disk size in MiB (default 96)")
    parser.add_argument("--check", action="store_true", help="read the image back and compare every file")
    args = parser.parse_args()
    if not args.source.is_dir():
        raise SystemExit("source directory does not exist: %s" % args.source)
    total_sectors = args.mib * 1024 * 1024 // SECTOR
    if total_sectors <= PART_START + RESERVED + 64:
        raise SystemExit("disk size is too small")
    part_sectors = total_sectors - PART_START
    root = scan_tree(args.source)
    root.name = ""
    image, used, capacity = build_image(root, part_sectors)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(
        "wrote %s bytes=%d fat_start=%d fat_count=%d clusters=%d/%d"
        % (args.output, len(image), PART_START, part_sectors, used, capacity)
    )
    if args.check:
        found = read_tree(image)
        expected = {}

        def collect(node, prefix):
            for child in node.children:
                path = prefix + child.name
                if child.is_dir:
                    collect(child, path + "/")
                else:
                    expected[path] = child.data

        collect(root, "")
        missing = sorted(set(expected) - set(found))
        extra = sorted(set(found) - set(expected))
        mismatched = sorted(path for path, data in expected.items() if found.get(path) != data)
        if missing or extra or mismatched:
            raise SystemExit(
                "FAT readback failed missing=%d extra=%d mismatched=%d"
                % (len(missing), len(extra), len(mismatched))
            )
        print("checked %d files" % len(expected))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        sys.exit(0)
