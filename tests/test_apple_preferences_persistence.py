#!/usr/bin/env python3
import hashlib
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.request


LCD_WIDTH = 176
LCD_HEIGHT = 132
LCD_CONTENT_Y = 16
LCD_MENU_WIDTH = 160
LCD_TITLE_WIDTH = 140
IPOD_TITLE_HASH = "e200b0b720c657661b9982b5133f5dabea78aec9f1151cdd316b8287cf1fdad0"
SETTINGS_TITLE_HASH = "5e70492ad425ba7c26b57dd746649dd744f971c96acff94d2e81e2487c8666fb"
MUSIC_TITLE_HASH = "9e5632af2c33e8c346d88a0f8c0254031f6a309fd53227efdd4290342a4ca1c1"


def unlink_if_exists(path):
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request_json(url, deadline):
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


def request_ok(url, deadline):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                if response.status == 200:
                    return
                last_error = RuntimeError(f"{url} returned HTTP {response.status}")
        except Exception as exc:
            last_error = exc
        time.sleep(0.05)
    raise RuntimeError(f"request failed for {url}: {last_error}")


def request_bytes(url, deadline):
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1.0) as response:
                if response.status != 200:
                    raise RuntimeError(f"{url} returned HTTP {response.status}")
                return response.read()
        except Exception as exc:
            last_error = exc
        time.sleep(0.05)
    raise RuntimeError(f"request failed for {url}: {last_error}")


def wait_status(base_url, predicate, deadline, description):
    while time.monotonic() < deadline:
        status = request_json(f"{base_url}/status.json", deadline)
        if predicate(status):
            return status
        time.sleep(0.02)
    raise RuntimeError(f"timed out waiting for {description}")


def wait_startup_activity(base_url, deadline):
    activity_seen = False
    while time.monotonic() < deadline:
        status = request_json(f"{base_url}/status.json", deadline)
        active = False
        if status.get("device_ticks", 0) >= 600_000:
            dump = request_json(
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


def wait_ticks(base_url, start, delta, deadline):
    return wait_status(
        base_url,
        lambda status: status.get("device_ticks", 0) >= start + delta,
        deadline,
        f"{delta} device ticks",
    )


def tap(base_url, button, deadline):
    before = request_json(f"{base_url}/status.json", deadline)
    request_ok(f"{base_url}/input?button={button}&tap=1", deadline)
    return wait_status(
        base_url,
        lambda status: status.get("input_events", 0) >= before.get("input_events", 0) + 2,
        deadline,
        f"{button} tap",
    )


def wheel_down(base_url, deadline):
    before = request_json(f"{base_url}/status.json", deadline)
    request_ok(f"{base_url}/input?wheel=down", deadline)
    delivered = wait_status(
        base_url,
        lambda status: status.get("input_events", 0) >= before.get("input_events", 0) + 1,
        deadline,
        "wheel event",
    )
    return wait_ticks(base_url, delivered.get("device_ticks", 0), 5_000, deadline)


def wheel_up(base_url, deadline):
    before = request_json(f"{base_url}/status.json", deadline)
    request_ok(f"{base_url}/input?wheel=up", deadline)
    delivered = wait_status(
        base_url,
        lambda status: status.get("input_events", 0) >= before.get("input_events", 0) + 1,
        deadline,
        "wheel event",
    )
    return wait_ticks(base_url, delivered.get("device_ticks", 0), 5_000, deadline)


def frame_bytes(base_url, status, deadline):
    frame = request_bytes(
        f"{base_url}/frame.rgba?raw=1&seq={status.get('frame_seq', 0)}",
        deadline,
    )
    if len(frame) != LCD_WIDTH * LCD_HEIGHT * 4:
        raise RuntimeError(f"unexpected RGBA frame size: {len(frame)}")
    return frame


def selected_row_center(frame):
    selected_rows = []
    for y in range(LCD_CONTENT_Y, LCD_HEIGHT):
        blue_pixels = 0
        for x in range(LCD_MENU_WIDTH):
            offset = (y * LCD_WIDTH + x) * 4
            red, green, blue = frame[offset:offset + 3]
            if blue > red + 40 and blue > green + 10 and blue > 120:
                blue_pixels += 1
        if blue_pixels >= 150:
            selected_rows.append(y)
    if not selected_rows:
        return None
    return sum(selected_rows) // len(selected_rows)


def title_hash(frame):
    title = bytearray()
    for y in range(LCD_CONTENT_Y):
        row_start = y * LCD_WIDTH * 4
        title.extend(frame[row_start:row_start + LCD_TITLE_WIDTH * 4])
    return hashlib.sha256(title).hexdigest()


def screen_state(base_url, status, deadline):
    frame = frame_bytes(base_url, status, deadline)
    return frame, title_hash(frame), selected_row_center(frame)


def write_frame_ppm(path, frame):
    rgb = bytearray()
    for offset in range(0, len(frame), 4):
        rgb.extend(frame[offset:offset + 3])
    path.write_bytes(
        f"P6\n{LCD_WIDTH} {LCD_HEIGHT}\n255\n".encode("ascii") + rgb
    )


def seek_row(base_url, direction, row, expected_title, limit, deadline, description):
    expected_center = 27 + row * 19
    observed = []
    for step in range(limit + 1):
        status = request_json(f"{base_url}/status.json", deadline)
        _, current_title, center = screen_state(base_url, status, deadline)
        if (
            current_title == expected_title
            and center is not None
            and abs(center - expected_center) <= 2
        ):
            return status
        observed.append((current_title, center))
        if step == limit:
            break
        next_direction = direction
        if center is not None:
            next_direction = "up" if center > expected_center else "down"
        if next_direction == "up":
            wheel_up(base_url, deadline)
        else:
            wheel_down(base_url, deadline)
    debug_path = pathlib.Path("apple-preferences-seek-failure.ppm").resolve()
    write_frame_ppm(debug_path, frame_bytes(base_url, status, deadline))
    raise RuntimeError(
        f"failed to seek {description} after {limit} {direction} events; "
        f"last selected-row centers: {observed[-4:]}; frame: {debug_path}"
    )


def return_to_title(base_url, expected_title, deadline, description):
    observed = []
    for _ in range(4):
        status = request_json(f"{base_url}/status.json", deadline)
        frame = frame_bytes(base_url, status, deadline)
        current_title = title_hash(frame)
        if current_title == expected_title:
            return status
        observed.append(current_title)
        status = tap(base_url, "menu", deadline)
        wait_ticks(base_url, status.get("device_ticks", 0), 20_000, deadline)
    raise RuntimeError(f"failed to return to {description}; title hashes: {observed}")


def navigate_to_repeat(base_url, deadline, first_boot):
    if first_boot:
        status = tap(base_url, "select", deadline)
        status = wait_ticks(base_url, status.get("device_ticks", 0), 25_000, deadline)
    else:
        status = return_to_title(base_url, IPOD_TITLE_HASH, deadline, "Main")
    status = seek_row(
        base_url, "down", 3, IPOD_TITLE_HASH, 20, deadline, "Main > Settings"
    )
    status = tap(base_url, "select", deadline)
    status = wait_ticks(base_url, status.get("device_ticks", 0), 25_000, deadline)
    return seek_row(
        base_url, "down", 3, SETTINGS_TITLE_HASH, 20, deadline, "Settings > Repeat"
    )


def start_seeded_track(base_url, deadline):
    status = return_to_title(base_url, IPOD_TITLE_HASH, deadline, "Main")
    status = seek_row(
        base_url, "up", 0, IPOD_TITLE_HASH, 20, deadline, "Main > Music"
    )
    status = tap(base_url, "select", deadline)
    status = wait_ticks(base_url, status.get("device_ticks", 0), 20_000, deadline)
    status = seek_row(
        base_url, "up", 0, MUSIC_TITLE_HASH, 20, deadline, "Music > Playlists"
    )
    status = seek_row(
        base_url, "down", 3, MUSIC_TITLE_HASH, 12, deadline, "Music > Songs"
    )
    status = tap(base_url, "select", deadline)
    status = wait_ticks(base_url, status.get("device_ticks", 0), 80_000, deadline)
    status = tap(base_url, "select", deadline)
    return wait_ticks(base_url, status.get("device_ticks", 0), 120_000, deadline)


def repeat_value_hash_from_frame(frame):
    value = bytearray()
    for y in range(75, 94):
        row_start = (y * LCD_WIDTH + 132) * 4
        value.extend(frame[row_start:row_start + 36 * 4])
    return hashlib.sha256(value).hexdigest()


def stable_repeat_frame(base_url, status, deadline):
    previous_hash = None
    while time.monotonic() < deadline:
        frame, current_title, center = screen_state(base_url, status, deadline)
        if (
            current_title == SETTINGS_TITLE_HASH
            and center is not None
            and abs(center - (27 + 3 * 19)) <= 2
        ):
            current_hash = repeat_value_hash_from_frame(frame)
            if current_hash == previous_hash:
                return status, frame, current_hash
            previous_hash = current_hash
        else:
            previous_hash = None
        status = wait_ticks(
            base_url,
            status.get("device_ticks", 0),
            5_000,
            deadline,
        )
    raise RuntimeError("timed out waiting for a stable Settings > Repeat frame")


def main():
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: test_apple_preferences_persistence.py "
            "EXE FIRMWARE DISK OUT LOG ROOT"
        )

    exe, firmware, disk, out_arg, log_arg, root = sys.argv[1:]
    output = pathlib.Path(out_arg)
    log_path = pathlib.Path(log_arg)
    unlink_if_exists(output)
    unlink_if_exists(log_path)
    port = free_port()
    base_url = f"http://127.0.0.1:{port}"
    command = [
        exe,
        "--profile",
        "apple",
        "--apple-diagnostics",
        "--firmware",
        firmware,
        "--disk",
        disk,
        "--disk-out",
        str(output),
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
        "8",
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
            deadline = time.monotonic() + 1_140.0
            settled = wait_startup_activity(base_url, deadline)
            baseline_writes = settled.get("disk_writes", 0)
            request_ok(f"{base_url}/hardware?rtc_usec_per_tick=1", deadline)

            repeat_off = navigate_to_repeat(base_url, deadline, True)
            repeat_off, _, off_hash = stable_repeat_frame(
                base_url, repeat_off, deadline
            )
            repeat_one = tap(base_url, "select", deadline)
            repeat_one = wait_ticks(
                base_url,
                repeat_one.get("device_ticks", 0),
                50_000,
                deadline,
            )
            repeat_one, repeat_one_frame, one_hash = stable_repeat_frame(
                base_url, repeat_one, deadline
            )
            if one_hash == off_hash:
                raise RuntimeError("Repeat toggle did not change the official-firmware frame")
            write_frame_ppm(
                log_path.with_name("apple-preferences-repeat-one.ppm"),
                repeat_one_frame,
            )

            checker_before = repeat_one.get("apple_preferences_hits", [0] * 9)[1]
            writer_before = repeat_one.get("apple_preferences_hits", [0] * 9)[5]
            clear_before = repeat_one.get("apple_preferences_hits", [0] * 9)[7]
            start_seeded_track(base_url, deadline)
            saved = wait_status(
                base_url,
                lambda status: len(status.get("apple_preferences_hits", [])) == 9
                and status["apple_preferences_hits"][1] > checker_before
                and status["apple_preferences_hits"][5] > writer_before
                and status["apple_preferences_hits"][7] > clear_before
                and status.get("disk_writes", 0) > baseline_writes,
                deadline,
                "native Preferences write and dirty clear after Music navigation",
            )
            if saved.get("lcd_overruns") != 0:
                raise RuntimeError("Preferences save run reported an LCD block overrun")

            old_ticks = saved.get("device_ticks", 0)
            request_ok(f"{base_url}/control?restart=apple-stage0", deadline)
            wait_status(
                base_url,
                lambda status: status.get("preset") == "apple-stage0"
                and status.get("device_ticks", old_ticks) < old_ticks,
                deadline,
                "Apple restart from the mutable snapshot",
            )
            wait_startup_activity(base_url, deadline)
            reloaded_repeat = navigate_to_repeat(base_url, deadline, False)
            reloaded_repeat, reloaded_frame, reloaded_hash = stable_repeat_frame(
                base_url, reloaded_repeat, deadline
            )
            if reloaded_hash != one_hash:
                debug_path = log_path.with_name("apple-preferences-repeat-reloaded.ppm")
                write_frame_ppm(debug_path, reloaded_frame)
                raise RuntimeError(
                    "reloaded official firmware did not render the persisted Repeat value; "
                    f"expected {one_hash}, got {reloaded_hash}; frame: {debug_path}"
                )
        finally:
            process.terminate()
            try:
                process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)

    if not output.exists() or output.stat().st_size != pathlib.Path(disk).stat().st_size:
        raise RuntimeError("mutable Apple disk snapshot was not saved at the source size")
    log_text = log_path.read_text(errors="replace")
    if f"loaded disk {output}" not in log_text:
        raise RuntimeError("Apple restart did not reload the mutable disk snapshot")
    print("Apple Preferences: Repeat change saved natively and survived snapshot restart")


if __name__ == "__main__":
    main()
