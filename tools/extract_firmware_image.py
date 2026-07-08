#!/usr/bin/env python3
import argparse
from pathlib import Path


def rd32(buf, off):
    return int.from_bytes(buf[off:off + 4], "little")


def fourcc(entry):
    return entry[4:8][::-1].decode("latin1", errors="replace")


def iter_images(data):
    if len(data) < 0x4200 or rd32(data, 0x100) != 0x5B68695D:
        raise SystemExit("not a wrapped firmware image")

    dir_off = rd32(data, 0x104)
    pos = dir_off + 0x200
    for index in range(64):
        if pos + 40 > len(data):
            break
        entry = data[pos:pos + 40]
        if rd32(entry, 0) == 0:
            break
        dev_off = rd32(entry, 12)
        length = rd32(entry, 16)
        yield {
            "index": index,
            "name": fourcc(entry),
            "file_off": dev_off + 0x200,
            "length": length,
            "addr": rd32(entry, 20),
            "entry": rd32(entry, 24),
            "checksum": rd32(entry, 28),
        }
        pos += 40


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware")
    ap.add_argument("name")
    ap.add_argument("output")
    args = ap.parse_args()

    data = Path(args.firmware).read_bytes()
    want = args.name.lower()
    for image in iter_images(data):
        if image["name"].lower() != want:
            continue
        begin = image["file_off"]
        end = begin + image["length"]
        if end > len(data):
            raise SystemExit("image extends past firmware file")
        Path(args.output).write_bytes(data[begin:end])
        print(
            "image name=%s index=%d file_off=0x%x len=0x%x addr=0x%08x entry=0x%08x checksum=0x%08x"
            % (
                image["name"],
                image["index"],
                image["file_off"],
                image["length"],
                image["addr"],
                image["entry"],
                image["checksum"],
            )
        )
        return
    raise SystemExit("image not found: %s" % args.name)


if __name__ == "__main__":
    main()
