#!/usr/bin/env python3
"""Repeatable, fidelity-oriented nano1g emulator benchmarks."""

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, NamedTuple, Optional, Tuple, Union


SUMMARY_RE = re.compile(r"\b([a-zA-Z0-9_]+)=([0-9]+/[0-9]+|0x[0-9a-fA-F]+|[0-9]+)")
DISTORTING_FLAGS = {
    "--apple-diagnostics",
    "--verbose",
    "--trace-pc",
    "--trace-mmio",
    "--host-profile",
}


class Workload(NamedTuple):
    name: str
    suite: str
    args: Tuple[str, ...]
    milestones: Tuple[Tuple[str, str], ...] = ()
    web: bool = False


def percentile95(values: List[float]) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * 0.95) - 1)]


def median(values: List[float]) -> float:
    ordered = sorted(values)
    count = len(ordered)
    middle = count // 2
    if count % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def file_sha256(path: Path) -> Optional[str]:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_checked(command: List[str], cwd: Path) -> None:
    result = subprocess.run(
        command, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        universal_newlines=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}\n{result.stderr}"
        )


def assemble_probe(root: Path, output: Path, source_name: str) -> Path:
    assembler = shutil.which("arm-none-eabi-as")
    objcopy = shutil.which("arm-none-eabi-objcopy")
    if not assembler or not objcopy:
        raise RuntimeError("arm-none-eabi-as and arm-none-eabi-objcopy are required")
    obj = output.with_suffix(".o")
    run_checked(
        [assembler, "-mcpu=arm7tdmi", "-o", str(obj), str(root / "tests" / source_name)],
        root,
    )
    run_checked([objcopy, "-O", "binary", str(obj), str(output)], root)
    return output


def prepare_fixtures(root: Path, scratch: Path) -> Dict[str, Path]:
    artifacts = root.parent / "artifacts"
    fixtures: Dict[str, Path] = {
        "apple_disk": artifacts / "images" / "ipodhd-apple-nano-sysinfo-preferences-probe.img",
        "apple_media_disk": artifacts / "images" / "ipodhd-apple-nano-media-probe.img",
        "rockbox_fw": artifacts / "firmware" / "rockbox.ipod",
        "rockbox_source_disk": artifacts / "images" / "ipodhd-rockbox-nano.img",
        "rockbox_content_source": artifacts / "images" / "ipodhd-rockbox-nano-content.img",
    }
    scratch.mkdir(parents=True, exist_ok=True)
    fixtures["apple_stage0"] = assemble_probe(
        root, scratch / "benchmark-apple-stage0.bin", "stage0_sysinfo_osos_probe.S"
    )
    fixtures["idle_probe"] = assemble_probe(
        root, scratch / "benchmark-idle.bin", "cpucon_idle_probe.S"
    )

    for key in ("apple_disk", "rockbox_fw", "rockbox_source_disk"):
        if not fixtures[key].is_file():
            raise RuntimeError(f"required fixture is missing: {fixtures[key]}")

    fixtures["rockbox_disk"] = scratch / "benchmark-rockbox-gpt.img"
    run_checked(
        [
            sys.executable,
            str(root / "tools" / "make_gpt_rockbox_disk.py"),
            str(fixtures["rockbox_source_disk"]),
            str(fixtures["rockbox_disk"]),
        ],
        root,
    )
    if fixtures["rockbox_content_source"].is_file():
        fixtures["rockbox_content_disk"] = scratch / "benchmark-rockbox-content-gpt.img"
        run_checked(
            [
                sys.executable,
                str(root / "tools" / "make_gpt_rockbox_disk.py"),
                str(fixtures["rockbox_content_source"]),
                str(fixtures["rockbox_content_disk"]),
            ],
            root,
        )
    return fixtures


def apple_args(fixtures: Dict[str, Path], max_insns: int) -> Tuple[str, ...]:
    return (
        "--profile", "apple",
        "--firmware", str(fixtures["apple_stage0"]),
        "--disk", str(fixtures["apple_disk"]),
        "--load-addr", "0x40000000",
        "--entry", "0x40000000",
        "--max-insns", str(max_insns),
        "--slice-insns", "512",
        "--timer-divider", "1",
        "--rtc-usec-per-tick", "8",
    )


def rockbox_args(fixtures: Dict[str, Path], max_insns: int, content: bool = False) -> Tuple[str, ...]:
    disk_key = "rockbox_content_disk" if content else "rockbox_disk"
    return (
        "--profile", "rockbox",
        "--firmware", str(fixtures["rockbox_fw"]),
        "--disk", str(fixtures[disk_key]),
        "--max-insns", str(max_insns),
        "--slice-insns", "512",
        "--timer-divider", "1",
    )


def build_workloads(fixtures: Dict[str, Path]) -> List[Workload]:
    apple_startup = "wait:285700,frame:startup"
    apple_menu = (
        "wait:285700,frame:language,select-down,wait:3000,select-up,wait:130000,"
        "frame:main,wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
        "wheel:+4,wait:5000,wheel:+4,wait:30000,select-down,wait:3000,"
        "select-up,wait:100000,frame:extras"
    )
    apple_active = (
        "wait:285700,select-down,wait:3000,select-up,wait:130000,"
        "wheel:+4,wait:3000,wheel:-4,wait:3000,wheel:+4,wait:3000,"
        "wheel:-4,wait:3000,wheel:+4,wait:3000,wheel:-4,wait:30000,frame:active"
    )
    apple_audio = (
        "wait:285700,select-down,wait:3000,select-up,wait:130000,"
        "select-down,wait:3000,select-up,wait:50000,"
        "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
        "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
        "wheel:+4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:20000,"
        "select-down,wait:3000,select-up,wait:80000,"
        "select-down,wait:3000,select-up,wait:120000,frame:audio"
    )
    rockbox_active = (
        "wait:2000000,wheel:+4,wait:20000,wheel:-4,wait:20000,"
        "wheel:+4,wait:20000,wheel:-4,wait:20000,frame:active"
    )
    rockbox_audio = (
        "wait:2000000,select-down,wait:50000,select-up,wait:300000,"
        "select-down,wait:50000,select-up,wait:300000,"
        "select-down,wait:50000,select-up,wait:8000000,frame:audio"
    )
    idle = (
        "--profile", "rockbox",
        "--firmware", str(fixtures["idle_probe"]),
        "--load-addr", "0x10000000",
        "--entry", "0x10000000",
        "--max-insns", "4294967296",
        "--slice-insns", "512",
        "--rtc-usec-per-tick", "8",
    )

    workloads = [
        Workload("apple-startup", "apple", apple_args(fixtures, 175_000_000) + ("--input", apple_startup), (("startup", "frame=startup"),)),
        Workload("apple-menu", "apple", apple_args(fixtures, 315_000_000) + ("--input", apple_menu), (("language", "frame=language"), ("main", "frame=main"), ("extras", "frame=extras"))),
        Workload("apple-active-ui", "apple", apple_args(fixtures, 315_000_000) + ("--input", apple_active), (("active", "frame=active"),)),
        Workload("apple-idle", "apple", idle),
        Workload("apple-web", "apple", apple_args(fixtures, 175_000_000) + ("--input", apple_startup), (("startup", "frame=startup"),), True),
        Workload("rockbox-startup", "rockbox", rockbox_args(fixtures, 2_500_000_000), (("firmware", "loaded ipod firmware model=nano"),)),
        Workload("rockbox-menu", "rockbox", rockbox_args(fixtures, 10_000_000_000)),
        Workload("rockbox-active-ui", "rockbox", rockbox_args(fixtures, 3_000_000_000) + ("--input", rockbox_active), (("active", "frame=active"),)),
        Workload("rockbox-idle", "rockbox", idle),
        Workload("rockbox-web", "rockbox", rockbox_args(fixtures, 2_500_000_000), (("firmware", "loaded ipod firmware model=nano"),), True),
    ]
    if "rockbox_content_disk" in fixtures:
        workloads.append(Workload("rockbox-audio", "rockbox", rockbox_args(fixtures, 8_000_000_000, True) + ("--battery-percent", "50", "--input", rockbox_audio), (("audio", "frame=audio"),)))
    if fixtures["apple_media_disk"].is_file():
        audio_args = list(apple_args(fixtures, 400_000_000))
        audio_args[audio_args.index(str(fixtures["apple_disk"]))] = str(fixtures["apple_media_disk"])
        workloads.append(Workload("apple-audio", "apple", tuple(audio_args) + ("--input", apple_audio), (("audio", "frame=audio"),)))
    return workloads


def reserve_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def poll_web(port: int, stop: threading.Event, counters: Dict[str, int]) -> None:
    last_frame = -1
    while not stop.wait(1.0 / 30.0):
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/state", timeout=0.2) as response:
                state = json.load(response)
            counters["state_requests"] += 1
            frame = int(state.get("frame_seq", -1))
            if frame != last_frame:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/frame.rgba", timeout=0.2) as response:
                    response.read()
                counters["frame_requests"] += 1
                last_frame = frame
        except OSError:
            continue


def parse_summary(output: str) -> Dict[str, Union[int, str]]:
    summary_line = next((line for line in reversed(output.splitlines()) if "summary guest_insns=" in line), None)
    if summary_line is None:
        raise RuntimeError("emulator output did not contain a summary")
    parsed: Dict[str, Union[int, str]] = {}
    for key, raw in SUMMARY_RE.findall(summary_line):
        if "/" in raw:
            parsed[key] = raw
        else:
            parsed[key] = int(raw, 0)
    return parsed


def run_once(binary: Path, root: Path, scratch: Path, workload: Workload, index: int) -> Dict[str, Any]:
    ppm = scratch / f"{workload.name}-{index}.ppm"
    command = [str(binary), *workload.args, "--ppm", str(ppm)]
    web_stop = threading.Event()
    web_counts = {"state_requests": 0, "frame_requests": 0}
    web_thread = None
    if workload.web:
        port = reserve_port()
        command.extend(("--web", str(port), "--web-no-hold"))
        web_thread = threading.Thread(target=poll_web, args=(port, web_stop, web_counts), daemon=True)

    if DISTORTING_FLAGS.intersection(command):
        raise RuntimeError(f"performance-distorting option in {workload.name}")

    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    if web_thread:
        web_thread.start()
    lines: List[str] = []
    milestone_times: Dict[str, float] = {}
    assert process.stdout is not None
    for line in process.stdout:
        lines.append(line)
        elapsed = time.perf_counter() - started
        for name, marker in workload.milestones:
            if name not in milestone_times and marker in line:
                milestone_times[name] = elapsed
    returncode = process.wait()
    wall_seconds = time.perf_counter() - started
    web_stop.set()
    if web_thread:
        web_thread.join(timeout=1.0)
    output = "".join(lines)
    if returncode != 0:
        raise RuntimeError(f"{workload.name} failed ({returncode}):\n{''.join(lines[-80:])}")

    stats = parse_summary(output)
    missing = [name for name, _ in workload.milestones if name not in milestone_times]
    if missing:
        raise RuntimeError(f"{workload.name} missed milestones: {', '.join(missing)}")
    if stats.get("unrouted_mmio") != "0/0":
        raise RuntimeError(f"{workload.name} encountered unrouted MMIO: {stats.get('unrouted_mmio')}")

    framebuffers = {"final": file_sha256(ppm)}
    for name, marker in workload.milestones:
        if marker.startswith("frame="):
            checkpoint = ppm.with_name(ppm.stem + "-" + marker[6:] + ppm.suffix)
            framebuffers[name] = file_sha256(checkpoint)
    missing_frames = [name for name, digest in framebuffers.items() if digest is None]
    if missing_frames:
        raise RuntimeError(f"{workload.name} missed framebuffer files: {', '.join(missing_frames)}")

    return {
        "wall_seconds": wall_seconds,
        "milestone_seconds": milestone_times,
        "framebuffer_sha256": framebuffers,
        "stats": stats,
        "web_requests": web_counts,
    }


def aggregate(workload: Workload, runs: List[Dict[str, Any]]) -> Dict[str, Any]:
    walls = [float(run["wall_seconds"]) for run in runs]
    metric_names = (
        "guest_insns", "scheduled_insns", "active_slices", "cpu_calls", "cop_calls",
        "halted_ticks", "fast_forwarded_ticks", "timing_boundaries", "ticks",
        "mmio_callbacks", "mmio_r", "mmio_w", "lcd_words", "lcd_block",
        "disk_reads", "disk_writes", "irq", "i2s_tx", "i2s_drained",
        "dma_audio_starts", "dma_audio_done", "dma_audio_bytes",
    )
    metrics: Dict[str, Dict[str, float]] = {}
    for name in metric_names:
        values = [float(run["stats"].get(name, 0)) for run in runs]
        metrics[name] = {"median": median(values), "p95": percentile95(values)}

    milestone_names = {name for run in runs for name in run["milestone_seconds"]}
    milestones = {}
    for name in sorted(milestone_names):
        values = [float(run["milestone_seconds"][name]) for run in runs]
        milestones[name] = {"median": median(values), "p95": percentile95(values)}

    guest_median = metrics["guest_insns"]["median"]
    scheduled_median = metrics["scheduled_insns"]["median"]
    frame_labels = {label for run in runs for label in run["framebuffer_sha256"]}
    framebuffer_hashes = {
        label: sorted({run["framebuffer_sha256"].get(label) for run in runs})
        for label in frame_labels
    }
    return {
        "suite": workload.suite,
        "wall_seconds": {"median": median(walls), "p95": percentile95(walls)},
        "guest_mips": guest_median / median(walls) / 1_000_000.0,
        "scheduled_mips": scheduled_median / median(walls) / 1_000_000.0,
        "milestone_seconds": milestones,
        "metrics": metrics,
        "framebuffer_hashes": framebuffer_hashes,
        "runs": runs,
    }


def compare_baseline(current: Dict[str, Any], baseline_path: Path) -> Dict[str, Any]:
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    comparisons = {}
    for name, result in current["workloads"].items():
        previous = baseline.get("workloads", {}).get(name)
        if not previous:
            continue
        old = float(previous["wall_seconds"]["median"])
        new = float(result["wall_seconds"]["median"])
        comparisons[name] = {
            "baseline_median_seconds": old,
            "current_median_seconds": new,
            "improvement_percent": (old - new) * 100.0 / old,
            "speedup": old / new,
            "framebuffer_match": previous.get("framebuffer_hashes") == result.get("framebuffer_hashes"),
        }
    return comparisons


def evaluate_acceptance(current: Dict[str, Any], comparisons: Dict[str, Any]) -> Dict[str, Any]:
    regressions = sorted(
        name for name, value in comparisons.items()
        if value["improvement_percent"] < -2.0
    )
    framebuffer_mismatches = sorted(
        name for name, value in comparisons.items()
        if not value["framebuffer_match"]
    )
    active_improvement = {}
    for suite in ("apple", "rockbox"):
        values = [
            value["improvement_percent"]
            for name, value in comparisons.items()
            if current["workloads"][name]["suite"] == suite
            and "idle" not in name and "web" not in name
        ]
        active_improvement[suite] = median(values) if values else None
    idle_speedups = {
        name: value["speedup"] for name, value in comparisons.items() if "idle" in name
    }
    evaluated = bool(comparisons)
    passed = (
        evaluated
        and not regressions
        and not framebuffer_mismatches
        and all(value is None or value >= 10.0 for value in active_improvement.values())
        and all(value >= 5.0 for value in idle_speedups.values())
    )
    return {
        "evaluated": evaluated,
        "passed": passed,
        "active_median_improvement_percent": active_improvement,
        "idle_speedups": idle_speedups,
        "regressions_over_2_percent": regressions,
        "framebuffer_mismatches": framebuffer_mismatches,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("apple", "rockbox", "all"), default="all")
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--workload", action="append", help="run only a named workload")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be at least 1")

    root = Path(__file__).resolve().parents[1]
    candidates = [root / "build-perf" / "nano1g.exe", root / "build-mingw" / "nano1g.exe", root / "build" / "nano1g"]
    binary = (args.binary or next((path for path in candidates if path.is_file()), None))
    if binary is None or not binary.is_file():
        parser.error("emulator binary not found; pass --binary")
    binary = binary.resolve()
    scratch = root / "tmp" / "benchmark"

    fixtures = prepare_fixtures(root, scratch)
    workloads = [workload for workload in build_workloads(fixtures) if args.suite == "all" or workload.suite == args.suite]
    if args.workload:
        selected = set(args.workload)
        workloads = [workload for workload in workloads if workload.name in selected]
        missing = selected.difference(workload.name for workload in workloads)
        if missing:
            parser.error(f"unknown or unavailable workload(s): {', '.join(sorted(missing))}")

    document: Dict[str, Any] = {
        "schema": 1,
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "machine": {"platform": platform.platform(), "processor": platform.processor()},
        "binary": str(binary),
        "binary_sha256": file_sha256(binary),
        "warmup_runs": 1,
        "measured_runs": args.runs,
        "workloads": {},
    }

    for workload in workloads:
        print(f"[{workload.name}] warm-up", flush=True)
        run_once(binary, root, scratch, workload, -1)
        measured = []
        for index in range(args.runs):
            print(f"[{workload.name}] run {index + 1}/{args.runs}", flush=True)
            measured.append(run_once(binary, root, scratch, workload, index))
        result = aggregate(workload, measured)
        document["workloads"][workload.name] = result
        print(
            f"  median={result['wall_seconds']['median']:.3f}s "
            f"p95={result['wall_seconds']['p95']:.3f}s "
            f"guest={result['guest_mips']:.2f} MIPS "
            f"scheduled={result['scheduled_mips']:.2f} MIPS",
            flush=True,
        )

    if args.baseline:
        document["baseline"] = str(args.baseline.resolve())
        document["comparison"] = compare_baseline(document, args.baseline)
        document["acceptance"] = evaluate_acceptance(document, document["comparison"])
        for name, comparison in document["comparison"].items():
            print(f"[{name}] improvement={comparison['improvement_percent']:+.2f}% speedup={comparison['speedup']:.3f}x")
        print("acceptance={}".format("PASS" if document["acceptance"]["passed"] else "FAIL"))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
