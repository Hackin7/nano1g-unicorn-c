#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


def rd16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def short_name(raw):
    base = raw[:8].decode("ascii", "replace").rstrip()
    ext = raw[8:11].decode("ascii", "replace").rstrip()
    return base if not ext else base + "." + ext


def lfn_part(entry):
    chars = []
    for off in (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30):
        v = rd16(entry, off)
        if v in (0x0000, 0xFFFF):
            continue
        chars.append(chr(v))
    return "".join(chars)


class Fat32:
    def __init__(self, data, part_index=1):
        self.data = data
        mbr = 0x1BE + part_index * 16
        self.part_lba = rd32(data, mbr + 8)
        self.part_sectors = rd32(data, mbr + 12)
        self.part_off = self.part_lba * 512
        self.bps = rd16(data, self.part_off + 11)
        self.spc = data[self.part_off + 13]
        self.reserved = rd16(data, self.part_off + 14)
        self.fats = data[self.part_off + 16]
        self.fatsz = rd32(data, self.part_off + 36)
        self.root_cluster = rd32(data, self.part_off + 44)
        self.fat_off = self.part_off + self.reserved * self.bps
        self.data_off = self.part_off + (self.reserved + self.fats * self.fatsz) * self.bps
        self.cluster_size = self.spc * self.bps

    def cluster_off(self, cluster):
        return self.data_off + (cluster - 2) * self.cluster_size

    def next_cluster(self, cluster):
        return rd32(self.data, self.fat_off + cluster * 4) & 0x0FFFFFFF

    def cluster_chain(self, start, max_clusters=4096):
        chain = []
        cluster = start
        while 2 <= cluster < 0x0FFFFFF8 and len(chain) < max_clusters:
            chain.append(cluster)
            cluster = self.next_cluster(cluster)
        return chain

    def read_chain(self, start):
        out = bytearray()
        for cluster in self.cluster_chain(start):
            off = self.cluster_off(cluster)
            out += self.data[off : off + self.cluster_size]
        return bytes(out)

    def walk_dir(self, cluster, prefix=""):
        directory = self.read_chain(cluster)
        lfn = []
        for off in range(0, len(directory), 32):
            entry = directory[off : off + 32]
            first = entry[0]
            if first == 0x00:
                break
            if first == 0xE5:
                lfn = []
                continue
            attr = entry[11]
            if attr == 0x0F:
                lfn.append(lfn_part(entry))
                continue
            if attr & 0x08:
                lfn = []
                continue
            name = "".join(reversed(lfn)) if lfn else short_name(entry[:11])
            lfn = []
            high = rd16(entry, 20)
            low = rd16(entry, 26)
            start = (high << 16) | low
            size = rd32(entry, 28)
            path = prefix + name
            is_dir = bool(attr & 0x10)
            yield (path, is_dir, start, size)
            if is_dir and name not in (".", "..") and start >= 2:
                yield from self.walk_dir(start, path + "/")

    def locate_offset(self, target):
        for path, is_dir, start, size in self.walk_dir(self.root_cluster):
            if is_dir or start < 2:
                continue
            remaining = size
            logical = 0
            for cluster in self.cluster_chain(start):
                off = self.cluster_off(cluster)
                take = min(self.cluster_size, remaining)
                if off <= target < off + take:
                    return path, logical + (target - off), size, cluster
                remaining -= take
                logical += take
                if remaining <= 0:
                    break
        return None

    def read_file(self, wanted):
        normalized = wanted.strip("/")
        for path, is_dir, start, size in self.walk_dir(self.root_cluster):
            if is_dir or path.lower() != normalized.lower():
                continue
            data = self.read_chain(start)
            return data[:size]
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--partition", type=int, default=1, help="zero-based MBR partition index")
    ap.add_argument("--offset", type=lambda x: int(x, 0))
    ap.add_argument("--extract", help="extract a FAT file by path")
    ap.add_argument("--output", help="output path for --extract")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    fat = Fat32(Path(args.image).read_bytes(), args.partition)
    print(
        "fat32 image=%s partition=%d part_lba=%d part_off=0x%x sectors=%d bps=%d spc=%d reserved=%d fats=%d fatsz=%d root_cluster=%d data_off=0x%x"
        % (
            args.image,
            args.partition,
            fat.part_lba,
            fat.part_off,
            fat.part_sectors,
            fat.bps,
            fat.spc,
            fat.reserved,
            fat.fats,
            fat.fatsz,
            fat.root_cluster,
            fat.data_off,
        )
    )
    if args.offset is not None:
        found = fat.locate_offset(args.offset)
        if found:
            path, file_off, size, cluster = found
            print(
                "offset=0x%x path=%s file_off=0x%x size=%d cluster=%d"
                % (args.offset, path, file_off, size, cluster)
            )
        else:
            print("offset=0x%x path=not-found" % args.offset)
    if args.extract:
        data = fat.read_file(args.extract)
        if data is None:
            raise SystemExit("file not found: %s" % args.extract)
        print("extract path=%s size=%d" % (args.extract, len(data)))
        if args.output:
            Path(args.output).write_bytes(data)
        else:
            print(data.hex())
    if args.list:
        for path, is_dir, start, size in fat.walk_dir(fat.root_cluster):
            kind = "dir" if is_dir else "file"
            print("%s cluster=%d size=%d %s" % (kind, start, size, path))


if __name__ == "__main__":
    main()
