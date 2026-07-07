#!/usr/bin/env python3
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise SystemExit(f"{path}: unsupported magic {magic!r}")
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxv = int(f.readline())
        if maxv != 255:
            raise SystemExit(f"{path}: unsupported max value {maxv}")
        data = f.read()
    return w, h, data


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ppm_compare.py expected.ppm actual.ppm")
    aw, ah, ad = read_ppm(sys.argv[1])
    bw, bh, bd = read_ppm(sys.argv[2])
    if (aw, ah) != (bw, bh):
        raise SystemExit(f"dimension mismatch: {(aw, ah)} != {(bw, bh)}")
    n = min(len(ad), len(bd))
    diff = sum(1 for i in range(n) if ad[i] != bd[i]) + abs(len(ad) - len(bd))
    total = max(len(ad), len(bd))
    ratio = diff / total if total else 0.0
    print(f"diff_bytes={diff} total_bytes={total} ratio={ratio:.6f}")
    return 1 if diff else 0


if __name__ == "__main__":
    raise SystemExit(main())
