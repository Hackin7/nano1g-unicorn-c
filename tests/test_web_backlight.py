#!/usr/bin/env python3
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.request


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(url, deadline, decode_json=True):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                return json.load(response) if decode_json else response.read()
        except Exception as exc:
            last_error = exc
        time.sleep(0.05)
    raise RuntimeError(f"request failed for {url}: {last_error}")


def wait_status(base_url, predicate, deadline, description):
    while time.monotonic() < deadline:
        status = request(f"{base_url}/status.json", deadline)
        if predicate(status):
            return status
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {description}")


def rgb_nonzero(frame):
    return sum(1 for offset in range(0, len(frame), 4) if any(frame[offset:offset + 3]))


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: test_web_backlight.py EXE FIRMWARE DISK LOG ROOT")

    exe, firmware, disk_arg, log_arg, root = sys.argv[1:]
    disk = pathlib.Path(disk_arg)
    log_path = pathlib.Path(log_arg)
    disk.write_bytes(bytes(512))
    log_path.unlink(missing_ok=True)
    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    command = [
        exe,
        "--run", "rockbox",
        "--firmware", firmware,
        "--disk", str(disk),
        "--run-forever",
        "--slice-insns", "1",
        "--web", str(port),
    ]

    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=root,
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        try:
            deadline = time.monotonic() + 20.0
            initial = wait_status(
                base_url,
                lambda s: s.get("backlight_mode") == "pwm"
                and s.get("backlight_on") is True
                and s.get("backlight_level") == 32
                and s.get("lcd_words", 0) >= 64,
                deadline,
                "guest PWM backlight and LCD frame",
            )
            initial_frame = request(f"{base_url}/frame.rgba", deadline, False)
            if len(initial_frame) != 176 * 132 * 4 or rgb_nonzero(initial_frame) < 100:
                raise RuntimeError("lit RGBA frame did not contain the guest LCD pixels")

            request(f"{base_url}/hardware?main_charger=1", deadline)
            off = wait_status(
                base_url,
                lambda s: s.get("backlight_mode") == "pwm"
                and s.get("backlight_on") is False,
                deadline,
                "guest PWM backlight off state",
            )
            if off.get("frame_seq", 0) <= initial.get("frame_seq", 0):
                raise RuntimeError("backlight transition did not advance frame sequence")
            off_frame = request(f"{base_url}/frame.rgba", deadline, False)
            if rgb_nonzero(off_frame) != 0:
                raise RuntimeError("backlight-off RGBA frame retained illuminated pixels")

            request(f"{base_url}/hardware?main_charger=0", deadline)
            wait_status(
                base_url,
                lambda s: s.get("backlight_on") is True
                and s.get("frame_seq", 0) > off.get("frame_seq", 0),
                deadline,
                "guest PWM backlight restored state",
            )
            restored_frame = request(f"{base_url}/frame.rgba", deadline, False)
            if restored_frame != initial_frame:
                raise RuntimeError("restored RGBA frame did not match the lit LCD frame")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

    print("web backlight: guest PWM status, frame sequencing, and RGBA intensity ok")


if __name__ == "__main__":
    main()
