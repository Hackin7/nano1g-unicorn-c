#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


WRAPPER_MAGIC = 0x5B68695D
HANDOFF_FUNC = 0x1388
HANDOFF_HELPER = 0x1084
CPU_ID_HELPER = 0x80200
PP32_HELPER = 0x598
PP36_HELPER = 0x5B8
NANO_PP_SELECTOR = 0x36
IPOD_PP_SELECTOR = 0x32
CPU_ID_BYTE = 0x55


def rd32(data, off):
    if off < 0 or off + 4 > len(data):
        raise SystemExit("read past end at 0x%x" % off)
    return struct.unpack_from("<I", data, off)[0]


def fourcc_entry_name(entry):
    return entry[4:8][::-1].decode("latin1", "replace")


def parse_wrapped_osos(data):
    if len(data) < 0x4200 or rd32(data, 0x100) != WRAPPER_MAGIC:
        return None
    directory = rd32(data, 0x104) + 0x200
    while directory + 40 <= len(data):
        if rd32(data, directory) == 0:
            break
        entry = data[directory : directory + 40]
        dev_off = rd32(entry, 12)
        length = rd32(entry, 16)
        payload = dev_off + 0x200
        if fourcc_entry_name(entry) == "osos":
            if payload + length > len(data):
                raise SystemExit("osos entry extends past firmware image")
            return data[payload : payload + length]
        directory += 40
    raise SystemExit("wrapped firmware has no osos entry")


def load_osos(path):
    data = Path(path).read_bytes()
    osos = parse_wrapped_osos(data)
    return osos if osos is not None else data


def require_word(data, off, value, label):
    got = rd32(data, off)
    if got != value:
        raise SystemExit("%s at 0x%x expected 0x%08x got 0x%08x" % (label, off, value, got))


def branch_target(pc, insn):
    imm = insn & 0x00FFFFFF
    if imm & 0x00800000:
        imm |= ~0x00FFFFFF
    return (pc + 8 + ((imm << 2) & 0xFFFFFFFF)) & 0xFFFFFFFF


def require_bl(data, off, target, label):
    insn = rd32(data, off)
    if (insn & 0xFF000000) != 0xEB000000:
        raise SystemExit("%s at 0x%x expected BL got 0x%08x" % (label, off, insn))
    got = branch_target(off, insn)
    if got != target:
        raise SystemExit("%s at 0x%x expected target 0x%x got 0x%x" % (label, off, target, got))


def check_contract(data):
    require_word(data, PP32_HELPER + 0x00, 0xE3A00207, "pp32 helper loads 0x70000000")
    require_word(data, PP32_HELPER + 0x10, 0xE3500032, "pp32 helper selector compare")
    require_word(data, PP36_HELPER + 0x00, 0xE3A00207, "pp36 helper loads 0x70000000")
    require_word(data, PP36_HELPER + 0x10, 0xE3500036, "pp36 helper selector compare")

    require_bl(data, HANDOFF_HELPER + 0x08, PP36_HELPER, "handoff slot helper pp36 test")
    require_bl(data, HANDOFF_HELPER + 0x14, PP32_HELPER, "handoff slot helper pp32 test")
    require_word(data, HANDOFF_HELPER + 0x34, 0x40018000, "handoff base legacy")
    require_word(data, HANDOFF_HELPER + 0x38, 0x40020000, "handoff base nano")

    require_word(data, CPU_ID_HELPER + 0x00, 0xE3A00206, "cpu id helper loads 0x60000000")
    require_word(data, CPU_ID_HELPER + 0x04, 0xE5D00000, "cpu id helper byte read")
    require_word(data, CPU_ID_HELPER + 0x08, 0xE3500055, "cpu id helper compares CPU byte")

    require_word(data, HANDOFF_FUNC + 0x00, 0xE92D4031, "handoff function prologue")
    require_word(data, HANDOFF_FUNC + 0x08, 0xE3A00018, "handoff slot offset")
    require_bl(data, HANDOFF_FUNC + 0x0C, HANDOFF_HELPER, "handoff slot helper call")
    require_word(data, HANDOFF_FUNC + 0x10, 0xE1A04000, "handoff pointer saved")
    require_bl(data, HANDOFF_FUNC + 0x14, 0x1BEC8, "handoff pre-init call")
    require_bl(data, HANDOFF_FUNC + 0x18, CPU_ID_HELPER, "handoff cpu id call")
    require_word(data, HANDOFF_FUNC + 0x24, 0xE5940004, "sysinfo pointer load")
    require_word(data, HANDOFF_FUNC + 0x2C, 0xE59030E0, "sysinfo model field load")
    require_word(data, HANDOFF_FUNC + 0x3C, 0xE5940000, "handoff tag load")
    require_word(data, HANDOFF_FUNC + 0x44, 0xE1500001, "handoff tag compare")
    require_word(data, HANDOFF_FUNC + 0x48, 0x05940004, "validated sysinfo reload")
    require_word(data, HANDOFF_FUNC + 0x74, 0x40006000, "handoff destination global")
    require_word(data, HANDOFF_FUNC + 0x78, 0x53797349, "handoff IsyS tag literal")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware_or_osos")
    args = ap.parse_args()

    osos = load_osos(args.firmware_or_osos)
    check_contract(osos)

    legacy_base = rd32(osos, HANDOFF_HELPER + 0x34)
    nano_base = rd32(osos, HANDOFF_HELPER + 0x38)
    slot_offset = rd32(osos, HANDOFF_FUNC + 0x08) & 0xFF
    helper_subtract = 0x100
    legacy_slot = legacy_base + slot_offset - helper_subtract
    nano_slot = nano_base + slot_offset - helper_subtract
    tag = rd32(osos, HANDOFF_FUNC + 0x78)

    print("osos_size=0x%x" % len(osos))
    print("pp_selectors legacy=0x%02x nano=0x%02x" % (IPOD_PP_SELECTOR, NANO_PP_SELECTOR))
    print("cpu_id_byte=0x%02x" % CPU_ID_BYTE)
    print("handoff_helper=0x%04x slot_offset=0x%x subtract=0x%x" %
          (HANDOFF_HELPER, slot_offset, helper_subtract))
    print("handoff_slots legacy=0x%08x nano=0x%08x" % (legacy_slot, nano_slot))
    print("handoff_tag=0x%08x/IsyS" % tag)
    print("sysinfo_pointer_source=[handoff+0x4]")
    print("sysinfo_model_word=[sysinfo+0xe0]")
    print("validated_sysinfo_global=0x4000608c")


if __name__ == "__main__":
    main()
