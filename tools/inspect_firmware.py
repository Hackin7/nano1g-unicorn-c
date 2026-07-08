#!/usr/bin/env python3
import struct
import sys
from pathlib import Path


def rd16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def rd32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def fourcc(entry):
    raw = entry[4:8]
    return raw[::-1].decode("latin1", errors="replace")


def main(argv):
    if len(argv) != 2:
        print("usage: inspect_firmware.py PATH", file=sys.stderr)
        return 2

    path = Path(argv[1])
    data = path.read_bytes()
    print("path=%s size=%d" % (path, len(data)))
    print("head=%s" % data[:64].hex())

    if len(data) < 0x4200 or rd32(data, 0x100) != 0x5B68695D:
        print("wrapped=no")
        return 0

    dir_off = rd32(data, 0x104)
    fmt = rd16(data, 0x10A)
    pos = dir_off + 0x200
    print("wrapped=yes fmt=%d dir_file_off=0x%x" % (fmt, pos))
    for index in range(64):
        if pos + 40 > len(data):
            break
        entry = data[pos:pos + 40]
        if rd32(entry, 0) == 0:
            break
        dev_off = rd32(entry, 12)
        length = rd32(entry, 16)
        addr = rd32(entry, 20)
        entry_off = rd32(entry, 24)
        checksum = rd32(entry, 28)
        print(
            "image[%02d] name=%s dev_off=0x%08x file_off=0x%08x len=0x%08x "
            "addr=0x%08x entry=0x%08x checksum=0x%08x"
            % (index, fourcc(entry), dev_off, dev_off + 0x200, length, addr, entry_off, checksum)
        )
        pos += 40
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
