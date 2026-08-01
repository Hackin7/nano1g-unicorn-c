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


def get_json(url, deadline):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                return json.load(response)
        except Exception as exc:
            last_error = exc
            time.sleep(0.05)
    raise RuntimeError(f"timed out reading {url}: {last_error}")


def wait_for(url, predicate, deadline, description):
    while time.monotonic() < deadline:
        status = get_json(url, deadline)
        if predicate(status):
            return status
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {description}")


def request_restart(base_url, preset, deadline):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(
                f"{base_url}/control?restart={preset}", timeout=1.0
            ) as response:
                if response.status == 200:
                    return
                last_error = RuntimeError(
                    f"restart={preset} returned HTTP {response.status}"
                )
        except Exception as exc:
            last_error = exc
        time.sleep(0.05)
    raise RuntimeError(f"restart={preset} failed: {last_error}")


def wait_for_log(path, needle, count, deadline):
    while time.monotonic() < deadline:
        if path.exists() and path.read_text(errors="replace").count(needle) >= count:
            return
        time.sleep(0.05)
    text = path.read_text(errors="replace") if path.exists() else ""
    raise RuntimeError(f"log did not contain {needle!r} {count} times:\n{text[-4000:]}")


def main():
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: test_web_disk_persistence.py EXE FIRMWARE SEED OUT LOG ROOT"
        )

    exe, firmware, seed_arg, out_arg, log_arg, root = sys.argv[1:]
    seed = pathlib.Path(seed_arg)
    output = pathlib.Path(out_arg)
    log_path = pathlib.Path(log_arg)
    seed_bytes = b"N1G! web persistence seed" + bytes(512 - 25)
    seed.write_bytes(seed_bytes)
    output.unlink(missing_ok=True)
    log_path.unlink(missing_ok=True)

    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    command = [
        exe,
        "--run",
        "rockbox",
        "--firmware",
        firmware,
        "--disk",
        str(seed),
        "--disk-out",
        str(output),
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
            wait_for(
                f"{base_url}/status.json",
                lambda status: status.get("disk_writes", 0) >= 256,
                deadline,
                "the guest sector write",
            )

            request_restart(base_url, "rockbox", deadline)
            wait_for_log(log_path, f"loaded disk {output}", 1, deadline)
            if output.read_bytes()[:4] != b"\x22\x11\x44\x33":
                raise RuntimeError("disk-out did not contain the guest-written marker")
            if seed.read_bytes() != seed_bytes:
                raise RuntimeError("source disk changed despite separate --disk-out")

            request_restart(base_url, "ipodlinux", deadline)
            wait_for(
                f"{base_url}/status.json",
                lambda status: status.get("preset") == "ipodlinux",
                deadline,
                "the iPod Linux preset",
            )
            request_restart(base_url, "rockbox", deadline)
            wait_for_log(log_path, f"loaded disk {output}", 2, deadline)
            if output.stat().st_size != 512 or output.read_bytes()[:4] != b"\x22\x11\x44\x33":
                raise RuntimeError("another preset overwrote the owned disk-out image")
            if seed.read_bytes() != seed_bytes:
                raise RuntimeError("source disk changed after browser preset switches")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

    exit_seed = seed.with_name(f"{seed.stem}-exit.img")
    exit_output = output.with_name(f"{output.stem}-exit.img")
    exit_seed.write_bytes(seed_bytes)
    exit_output.unlink(missing_ok=True)
    completed = subprocess.run(
        [
            exe,
            "--run",
            "rockbox",
            "--firmware",
            firmware,
            "--disk",
            str(exit_seed),
            "--disk-out",
            str(exit_output),
            "--max-insns",
            "2000",
            "--slice-insns",
            "1",
        ],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=10.0,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if completed.returncode != 0:
        raise RuntimeError(f"clean-exit persistence run failed:\n{completed.stdout}")
    if exit_output.read_bytes()[:4] != b"\x22\x11\x44\x33":
        raise RuntimeError("clean exit did not save the guest-written marker")
    if exit_seed.read_bytes() != seed_bytes:
        raise RuntimeError("clean exit modified its source disk")

    print(
        "web disk persistence: guest write saved, reloaded, preset-isolated, "
        "and clean-exit saved"
    )


if __name__ == "__main__":
    main()
