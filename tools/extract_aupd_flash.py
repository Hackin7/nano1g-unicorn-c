#!/usr/bin/env python3
import argparse
import hashlib
import struct
from pathlib import Path


PYLD_MAGIC = 0x50796C64
FWUP_MAGIC = 0x46775570
FWUP_RECORD_SIZE = 28
FLASH_ENTRY_SIZE = 0x28

FLASH_TYPES = {
    b"hslfksid": "diskmode",
    b"hslfgaid": "diagmode",
    b"hslfogol": "logo",
    b"hslfnacs": "diskscan",
    b"hslfscmv": "vmcs",
}

FWUP_KEYS = {
    0x466C7368: "Flsh",
    0x666C7368: "flsh",
}


def rd32(data, off):
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]


def fourcc_be(value):
    raw = struct.pack(">I", value)
    return "".join(chr(b) if 32 <= b < 127 else "." for b in raw)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def iter_word(data, value):
    needle = struct.pack("<I", value)
    pos = data.find(needle)
    while pos >= 0:
        yield pos
        pos = data.find(needle, pos + 1)


def iter_pyld(data):
    for off in iter_word(data, PYLD_MAGIC):
        header_size = rd32(data, off + 4)
        payload_size = rd32(data, off + 8)
        flags = rd32(data, off + 12)
        if header_size is None or payload_size is None or flags is None:
            continue
        if header_size < 0x10 or header_size > 0x1000:
            continue
        yield {
            "off": off,
            "header_size": header_size,
            "payload_size": payload_size,
            "flags": flags,
        }


def fwup_record(data, off):
    if off + FWUP_RECORD_SIZE > len(data) or rd32(data, off) != FWUP_MAGIC:
        return None
    return {
        "off": off,
        "next_or_len": rd32(data, off + 4),
        "key": rd32(data, off + 8),
        "size": rd32(data, off + 12),
        "arg0": rd32(data, off + 16),
        "arg1": rd32(data, off + 20),
        "checksum": rd32(data, off + 24),
    }


def iter_fwup_records(data):
    for off in iter_word(data, FWUP_MAGIC):
        rec = fwup_record(data, off)
        if rec is not None and rec["key"] in FWUP_KEYS and rec["next_or_len"] == FWUP_RECORD_SIZE:
            yield rec


def flash_entry_name(raw):
    return FLASH_TYPES.get(raw, raw[::-1].decode("latin1", "replace"))


def looks_like_flash_entry(data, off):
    if off + FLASH_ENTRY_SIZE > len(data):
        return False
    raw = data[off : off + 8]
    if raw in FLASH_TYPES:
        return True
    if rd32(data, off) == 0 or rd32(data, off) == 0xFFFFFFFF:
        return False
    return raw[:4] == b"hslf"


def iter_flash_dirs(data):
    candidates = []
    for marker in FLASH_TYPES:
        pos = data.find(marker)
        while pos >= 0:
            if looks_like_flash_entry(data, pos):
                candidates.append(pos)
            pos = data.find(marker, pos + 1)

    consumed_until = -1
    for pos in sorted(set(candidates)):
        if pos < consumed_until:
            continue
        entries = []
        cur = pos
        while looks_like_flash_entry(data, cur):
            raw = data[cur : cur + 8]
            words = [rd32(data, cur + i) for i in range(0, FLASH_ENTRY_SIZE, 4)]
            payload_off = rd32(data, cur + 0x0C)
            payload_size = rd32(data, cur + 0x10)
            load_addr = rd32(data, cur + 0x14)
            entries.append(
                {
                    "off": cur,
                    "name": flash_entry_name(raw),
                    "raw": raw,
                    "payload_off": payload_off,
                    "payload_size": payload_size,
                    "load_addr": load_addr,
                    "words": words,
                }
            )
            cur += FLASH_ENTRY_SIZE
        consumed_until = cur
        yield pos, entries


def reset_vector_hits(blob):
    if len(blob) < 0x20:
        return 0
    hits = 0
    for off in range(0, 0x20, 4):
        word = rd32(blob, off)
        if word is None:
            continue
        is_branch = (word & 0x0F000000) == 0x0A000000
        is_ldr_pc = (word & 0x0F7F0000) == 0x051F0000 and ((word >> 12) & 0xF) == 15
        if is_branch or is_ldr_pc:
            hits += 1
    return hits


def payload_for_entry(data, entry):
    off = entry["payload_off"]
    size = entry["payload_size"]
    if off is None or size is None or off + size > len(data):
        return None
    return data[off : off + size]


def list_image(path):
    data = Path(path).read_bytes()
    print("image=%s size=0x%x sha256=%s" % (path, len(data), sha256(data)))

    for pyld in iter_pyld(data):
        print(
            "pyld off=0x%x header=0x%x payload_size=0x%x flags=0x%x"
            % (pyld["off"], pyld["header_size"], pyld["payload_size"], pyld["flags"])
        )

    for rec in iter_fwup_records(data):
        key_name = FWUP_KEYS.get(rec["key"], "unknown")
        print(
            "fwup off=0x%x key=0x%08x/%s/%s next_or_len=0x%x size=0x%x arg0=0x%x arg1=0x%x checksum=0x%08x"
            % (
                rec["off"],
                rec["key"],
                fourcc_be(rec["key"]),
                key_name,
                rec["next_or_len"],
                rec["size"],
                rec["arg0"],
                rec["arg1"],
                rec["checksum"],
            )
        )

    for dir_off, entries in iter_flash_dirs(data):
        print("flash_dir off=0x%x entries=%d" % (dir_off, len(entries)))
        for entry in entries:
            payload = payload_for_entry(data, entry)
            if payload is None:
                detail = "payload=out-of-range"
            else:
                detail = "payload_sha256=%s reset_vector_hits=%d head=%s" % (
                    sha256(payload),
                    reset_vector_hits(payload),
                    payload[:16].hex(),
                )
            print(
                "  entry name=%s off=0x%x payload_off=0x%x size=0x%x load=0x%08x words=%s %s"
                % (
                    entry["name"],
                    entry["off"],
                    entry["payload_off"],
                    entry["payload_size"],
                    entry["load_addr"],
                    ",".join("0x%08x" % w for w in entry["words"]),
                    detail,
                )
            )


def extract_entry(path, name, output):
    data = Path(path).read_bytes()
    matches = []
    for _dir_off, entries in iter_flash_dirs(data):
        for entry in entries:
            if entry["name"].lower() == name.lower():
                matches.append(entry)

    if not matches:
        raise SystemExit("flash entry not found: %s" % name)
    if len(matches) > 1:
        raise SystemExit("flash entry is ambiguous: %s matched %d entries" % (name, len(matches)))

    payload = payload_for_entry(data, matches[0])
    if payload is None:
        raise SystemExit("flash entry payload is out of range: %s" % name)
    Path(output).write_bytes(payload)
    print(
        "extracted name=%s output=%s size=0x%x sha256=%s reset_vector_hits=%d"
        % (matches[0]["name"], output, len(payload), sha256(payload), reset_vector_hits(payload))
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="decrypted AUPD image")
    ap.add_argument("--extract", metavar="NAME", help="extract a named flash-directory payload")
    ap.add_argument("--output", help="output path for --extract")
    args = ap.parse_args()

    if args.extract:
        if not args.output:
            raise SystemExit("--extract requires --output")
        extract_entry(args.image, args.extract, args.output)
    else:
        list_image(args.image)


if __name__ == "__main__":
    main()
