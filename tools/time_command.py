import subprocess
import sys
import time


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: time_command.py COMMAND [ARGS...]", file=sys.stderr)
        return 2
    start = time.perf_counter()
    proc = subprocess.run(sys.argv[1:])
    elapsed = time.perf_counter() - start
    print(f"elapsed_sec={elapsed:.3f}")
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
