#!/usr/bin/env python3
import argparse
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ppm")
    args = ap.parse_args()

    data = Path(args.ppm).read_bytes()
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6":
        raise SystemExit("expected binary P6 PPM")
    pixels = parts[3]
    triplets = [pixels[i:i + 3] for i in range(0, len(pixels), 3)]
    nonblack = sum(1 for px in triplets if px != b"\0\0\0")
    unique = len(set(triplets))
    print(f"bytes={len(data)} pixel_bytes={len(pixels)} pixels={len(triplets)} nonblack={nonblack} unique_colors={unique}")


if __name__ == "__main__":
    main()
