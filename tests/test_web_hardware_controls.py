#!/usr/bin/env python3
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(url, deadline, expect_status=200):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                if response.status != expect_status:
                    raise RuntimeError(f"{url} returned HTTP {response.status}")
                return json.load(response)
        except urllib.error.HTTPError as exc:
            if exc.code == expect_status:
                return json.load(exc)
            last_error = exc
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


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: test_web_hardware_controls.py EXE FIRMWARE DISK LOG ROOT")

    exe, firmware, disk_arg, log_arg, root = sys.argv[1:]
    disk = pathlib.Path(disk_arg)
    log_path = pathlib.Path(log_arg)
    disk.write_bytes(bytes(512))
    log_path.unlink(missing_ok=True)
    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    command = [
        exe,
        "--battery-percent",
        "88",
        "--main-charger",
        "--usb-charger",
        "--hold-switch",
        "--run",
        "rockbox",
        "--firmware",
        firmware,
        "--disk",
        str(disk),
        "--run-forever",
        "--slice-insns",
        "1",
        "--web",
        str(port),
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
                lambda s: s.get("battery_percent") == 88 and s.get("hold") is True,
                deadline,
                "initial CLI hardware state",
            )
            if not initial.get("main_charger") or not initial.get("usb_charger"):
                raise RuntimeError("preset application discarded initial charger state")

            changed = request(
                f"{base_url}/hardware?battery=37&main_charger=0&usb_charger=1&hold=0",
                deadline,
            )
            if changed != {
                "ok": True,
                "battery_percent": 37,
                "main_charger": False,
                "usb_charger": True,
                "hold": False,
            }:
                raise RuntimeError(f"unexpected hardware response: {changed}")

            request(f"{base_url}/hardware?hold=1", deadline)
            before = request(f"{base_url}/status.json", deadline)
            request(f"{base_url}/input?button=select&tap=1", deadline)
            request(f"{base_url}/input?wheel=down", deadline)
            held = wait_status(
                base_url,
                lambda s: s.get("input_suppressed", 0) >= before.get("input_suppressed", 0) + 2,
                deadline,
                "hold-suppressed browser input",
            )
            if held.get("opto_buttons") != "0x00000000":
                raise RuntimeError("held browser input reached the optical controller")

            request(f"{base_url}/hardware?battery=101", deadline, expect_status=400)
            final = request(f"{base_url}/status.json", deadline)
            if final.get("battery_percent") != 37:
                raise RuntimeError("invalid battery update changed live hardware state")

            request(f"{base_url}/control?restart=rockbox", deadline)
            restarted = wait_status(
                base_url,
                lambda s: s.get("input_suppressed") == 0
                and s.get("input") == "none"
                and s.get("battery_percent") == 37
                and s.get("main_charger") is False
                and s.get("usb_charger") is True
                and s.get("hold") is True,
                deadline,
                "hardware state after browser preset restart",
            )
            if restarted.get("preset") != "rockbox":
                raise RuntimeError("browser restart selected the wrong preset")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

    print(
        "web hardware controls: live power state, hold suppression, restart "
        "persistence, and validation ok"
    )


if __name__ == "__main__":
    main()
