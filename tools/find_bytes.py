#!/usr/bin/env python3
import argparse
from pathlib import Path


def parse_pattern(text):
    if text.startswith("hex:"):
        return bytes.fromhex(text[4:])
    if text.startswith("u32le:"):
        return int(text[6:], 0).to_bytes(4, "little")
    return text.encode("latin1")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("patterns", nargs="+")
    args = ap.parse_args()

    data = Path(args.file).read_bytes()
    print("file=%s size=%d" % (args.file, len(data)))
    for text in args.patterns:
        pat = parse_pattern(text)
        print("%s offset=%d" % (text, data.find(pat)))


if __name__ == "__main__":
    main()
