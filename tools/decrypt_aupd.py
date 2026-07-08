#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


MARKER_WORD_OFFSETS = [0x05, 0x25, 0x6F, 0x69, 0x15, 0x4D, 0x40, 0x34]
KEY_CONSTANT = 0x54C3A298


def rd16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def rd32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def fourcc(entry):
    return entry[4:8][::-1].decode("latin1", "replace")


def iter_images(data):
    if len(data) < 0x4200 or rd32(data, 0x100) != 0x5B68695D:
        raise SystemExit("not a wrapped firmware image")
    dir_off = rd32(data, 0x104)
    pos = dir_off + 0x200
    for index in range(64):
        if pos + 40 > len(data):
            break
        entry = data[pos : pos + 40]
        if rd32(entry, 0) == 0:
            break
        dev_off = rd32(entry, 12)
        yield {
            "index": index,
            "name": fourcc(entry),
            "security_off": dev_off,
            "file_off": dev_off + 0x200,
            "length": rd32(entry, 16),
            "addr": rd32(entry, 20),
            "entry": rd32(entry, 24),
            "checksum": rd32(entry, 28),
        }
        pos += 40


def find_image(data, name):
    want = name.lower()
    for image in iter_images(data):
        if image["name"].lower() == want:
            start = image["file_off"]
            end = start + image["length"]
            if end > len(data):
                raise SystemExit("%s image extends past firmware file" % name)
            return image, data[start:end]
    raise SystemExit("%s image not found" % name)


def marker_enabled(marker):
    mask = marker & 0xFF
    spread = mask | (mask << 8) | (mask << 16) | (mask << 24)
    decoded = marker ^ spread
    a = (decoded >> 24) & 0xFF
    b = (decoded >> 16) & 0xFF
    c = (decoded >> 8) & 0xFF
    if a == 0:
        return False
    if not (a < b < c):
        return False
    return ((a & 0x0F) > (b & 0x0F) > (c & 0x0F)) and ((c & 0x0F) != 0)


def derive_key(security):
    if len(security) < 512:
        raise SystemExit("security block is shorter than 512 bytes")
    enabled = []
    for i, word_off in enumerate(MARKER_WORD_OFFSETS):
        marker = rd32(security, word_off * 4)
        if not marker_enabled(marker):
            continue
        if i + 1 >= len(MARKER_WORD_OFFSETS):
            raise SystemExit("last marker is enabled; no following marker slot for key seed")
        pos = MARKER_WORD_OFFSETS[i + 1] * 4 + 4
        key = 0
        for _ in range(2):
            word = rd32(security, pos)
            key = (marker ^ word ^ KEY_CONSTANT) & 0xFFFFFFFF
            pos += 4

        r1 = 0x6F
        for count in range(2, 128, 2):
            r2 = rd32(security, count * 4)
            r12 = rd32(security, count * 4 + 4)
            r14 = (r2 | (r12 >> 16)) & 0xFFFFFFFF
            low_mix = ((r2 & 0xFFFF) | r12) & 0xFFFFFFFF
            r1 = ((r1 ^ r14) + low_mix) & 0xFFFFFFFF

        key ^= r1
        flipped = struct.unpack(">I", struct.pack("<I", key))[0]
        enabled.append((i, marker, flipped))

    if not enabled:
        return None, []
    return enabled[-1][2], enabled


def derive_candidate_key(security, i):
    marker = rd32(security, MARKER_WORD_OFFSETS[i] * 4)
    next_i = (i + 1) % len(MARKER_WORD_OFFSETS)
    pos = MARKER_WORD_OFFSETS[next_i] * 4 + 4
    key = 0
    for _ in range(2):
        word = rd32(security, pos)
        key = (marker ^ word ^ KEY_CONSTANT) & 0xFFFFFFFF
        pos += 4

    r1 = 0x6F
    for count in range(2, 128, 2):
        r2 = rd32(security, count * 4)
        r12 = rd32(security, count * 4 + 4)
        r14 = (r2 | (r12 >> 16)) & 0xFFFFFFFF
        low_mix = ((r2 & 0xFFFF) | r12) & 0xFFFFFFFF
        r1 = ((r1 ^ r14) + low_mix) & 0xFFFFFFFF

    key ^= r1
    return marker, struct.unpack(">I", struct.pack("<I", key))[0]


def rc4_crypt(key_bytes, data):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key_bytes[i % len(key_bytes)]) & 0xFF
        s[i], s[j] = s[j], s[i]

    out = bytearray(len(data))
    i = 0
    j = 0
    for n, b in enumerate(data):
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        k = s[(s[i] + s[j]) & 0xFF]
        out[n] = b ^ k
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware")
    ap.add_argument("output")
    ap.add_argument("--raw-aupd", help="also write the encrypted aupd payload")
    ap.add_argument("--try-all", action="store_true", help="print scores for all marker-derived candidate keys")
    args = ap.parse_args()

    fw = Path(args.firmware).read_bytes()
    image, aupd = find_image(fw, "aupd")
    security_off = image["security_off"]
    if security_off + 512 > len(fw):
        raise SystemExit("aupd security block extends past firmware file")
    security = fw[security_off : security_off + 512]

    key, markers = derive_key(security)
    print(
        "aupd index=%d file_off=0x%x len=0x%x addr=0x%08x entry=0x%08x checksum=0x%08x"
        % (
            image["index"],
            image["file_off"],
            image["length"],
            image["addr"],
            image["addr"] + image["entry"],
            image["checksum"],
        )
    )
    for idx, marker, marker_key in markers:
        print("marker[%d]=0x%08x key=0x%08x" % (idx, marker, marker_key))

    if args.raw_aupd:
        Path(args.raw_aupd).write_bytes(aupd)
        print("wrote encrypted aupd %s size=%d" % (args.raw_aupd, len(aupd)))

    def score(data):
        probes = [b"diskmode", b"diskscan", b"diag", b"boot", b"IsyS", b"[hi]", b"flsh", b"disk"]
        printable = sum(1 for b in data[:4096] if b in b"\r\n\t" or 32 <= b < 127)
        zeros = data[:4096].count(0)
        arm_vectors = 0
        for off in range(0, min(len(data), 64), 4):
            word = rd32(data, off)
            if (word & 0x0F000000) == 0x0A000000 or word in (0xE59FF018, 0xEA000000):
                arm_vectors += 25
        return sum(100 for p in probes if p in data) + printable + min(zeros, 256) + arm_vectors

    if args.try_all:
        body = aupd
        for i, word_off in enumerate(MARKER_WORD_OFFSETS):
            marker, candidate = derive_candidate_key(security, i)
            for order, key_bytes in (("be", struct.pack(">I", candidate)), ("le", struct.pack("<I", candidate))):
                dec = rc4_crypt(key_bytes, body)
                print(
                    "candidate[%d] word_off=0x%x marker=0x%08x enabled=%d key=0x%08x order=%s score=%d head=%s"
                    % (
                        i,
                        word_off,
                        marker,
                        1 if marker_enabled(marker) else 0,
                        candidate,
                        order,
                        score(dec),
                        dec[:16].hex(),
                    )
                )

    if key is None:
        Path(args.output).write_bytes(aupd)
        print("aupd appears unprotected; wrote payload after security block")
        return

    key_be = struct.pack(">I", key)
    key_le = struct.pack("<I", key)
    dec_be = rc4_crypt(key_be, aupd)
    dec_le = rc4_crypt(key_le, aupd)

    chosen_name = "be" if score(dec_be) >= score(dec_le) else "le"
    chosen = dec_be if chosen_name == "be" else dec_le
    Path(args.output).write_bytes(chosen)
    print(
        "wrote decrypted aupd %s size=%d key_order=%s score_be=%d score_le=%d head=%s"
        % (args.output, len(chosen), chosen_name, score(dec_be), score(dec_le), chosen[:32].hex())
    )


if __name__ == "__main__":
    main()
