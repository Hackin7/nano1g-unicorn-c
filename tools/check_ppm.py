import argparse
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"{path}: unsupported PPM magic {magic!r}")

        dims = f.readline().strip()
        while dims.startswith(b"#"):
            dims = f.readline().strip()
        width_s, height_s = dims.split()
        maxval = int(f.readline().strip())
        if maxval != 255:
            raise ValueError(f"{path}: unsupported maxval {maxval}")
        data = f.read()

    width = int(width_s)
    height = int(height_s)
    expected = width * height * 3
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} pixel bytes, got {len(data)}")
    return width, height, data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ppm")
    parser.add_argument("--min-nonblack", type=int, default=1)
    parser.add_argument("--max-nonblack", type=int, default=None)
    parser.add_argument("--min-unique", type=int, default=1)
    args = parser.parse_args()

    width, height, data = read_ppm(args.ppm)
    pixels = [data[i:i + 3] for i in range(0, len(data), 3)]
    nonblack = sum(1 for p in pixels if p != b"\x00\x00\x00")
    unique = len(set(pixels))

    print(
        f"ppm={args.ppm} size={width}x{height} "
        f"nonblack={nonblack} unique_colors={unique}"
    )

    if nonblack < args.min_nonblack:
        print(
            f"nonblack pixel count {nonblack} below required {args.min_nonblack}",
            file=sys.stderr,
        )
        return 1
    if args.max_nonblack is not None and nonblack > args.max_nonblack:
        print(
            f"nonblack pixel count {nonblack} above allowed {args.max_nonblack}",
            file=sys.stderr,
        )
        return 1
    if unique < args.min_unique:
        print(
            f"unique color count {unique} below required {args.min_unique}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
