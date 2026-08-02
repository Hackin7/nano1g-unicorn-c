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


def request(url, deadline):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                if response.status != 200:
                    raise RuntimeError(f"{url} returned HTTP {response.status}")
                return json.load(response)
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


def wait_startup_activity(base_url, deadline):
    activity_seen = False
    while time.monotonic() < deadline:
        status = request(f"{base_url}/status.json", deadline)
        active = False
        if status.get("device_ticks", 0) >= 600_000:
            # Native activity-manager slots scanned by the idle guard.
            dump = request(
                f"{base_url}/dump32?addr=0x107a65b0&count=11",
                deadline,
            )
            words = dump.get("words")
            if not isinstance(words, list) or len(words) != 11:
                raise RuntimeError(f"unexpected activity-table dump: {dump}")
            active = any(int(word, 16) != 0 for word in words)
            activity_seen = activity_seen or active
        if (
            activity_seen
            and not active
            and status.get("lcd_blocks", 0) > 0
            and status.get("backlight_on") is True
        ):
            return status
        time.sleep(0.5)
    raise RuntimeError("timed out waiting for Apple startup activity to settle")


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: test_apple_idle_wake.py EXE FIRMWARE DISK LOG ROOT")

    exe, firmware, disk, log_arg, root = sys.argv[1:]
    log_path = pathlib.Path(log_arg)
    log_path.unlink(missing_ok=True)
    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    command = [
        exe,
        "--profile",
        "apple",
        "--firmware",
        firmware,
        "--disk",
        disk,
        "--load-addr",
        "0x40000000",
        "--entry",
        "0x40000000",
        "--run-forever",
        "--slice-insns",
        "512",
        "--timer-divider",
        "1",
        "--rtc-usec-per-tick",
        "256",
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
            deadline = time.monotonic() + 660.0
            settled = wait_startup_activity(base_url, deadline)
            if settled.get("lcd_overruns") != 0:
                raise RuntimeError("Apple startup reported an LCD block overrun")

            before_events = settled.get("input_events", 0)
            request(f"{base_url}/input?button=menu&tap=1", deadline)
            armed = wait_status(
                base_url,
                lambda s: s.get("input_events", 0) >= before_events + 2
                and s.get("backlight_on") is True,
                deadline,
                "native input event to rearm idle timer",
            )
            armed_frame = armed.get("frame_seq", 0)

            blanked = wait_status(
                base_url,
                lambda s: s.get("backlight_on") is False,
                deadline,
                "native Apple idle backlight-off transition",
            )
            if blanked.get("frame_seq", 0) <= armed_frame:
                raise RuntimeError("backlight-off transition did not publish a new frame")

            before_wake_events = blanked.get("input_events", 0)
            request(f"{base_url}/input?button=menu&tap=1", deadline)
            woke = wait_status(
                base_url,
                lambda s: s.get("input_events", 0) >= before_wake_events + 2
                and s.get("backlight_on") is True,
                deadline,
                "native button wake transition",
            )
            if woke.get("lcd_overruns") != 0:
                raise RuntimeError("Apple idle/wake cycle reported an LCD block overrun")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

    print("Apple idle/wake: post-startup native timeout-off and button wake ok")


if __name__ == "__main__":
    main()
