from pathlib import Path


data = Path(r"..\artifacts\firmware\apple_nano_14.5.3.1_fw.bin").read_bytes()
patterns = [
    0xC0DEBABE,
    0x11111129,
    0x22222229,
    0x33333329,
    0x4444440C,
    0x5555550C,
    0x5E285E28,
]

for pattern in patterns:
    print(f"0x{pattern:08x} {data.find(pattern.to_bytes(4, 'little'))}")
