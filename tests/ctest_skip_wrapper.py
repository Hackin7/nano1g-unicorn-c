#!/usr/bin/env python3
"""Map a voluntary CMake smoke skip onto CTest's skip exit code.

Smoke scripts report a missing fixture or toolchain with
``message(WARNING "skipping ... missing")`` and then ``return()``.
``cmake -P`` then exits 0, so CTest counts the test as passed. This wrapper
exits 77 instead. Register the test with ``SKIP_RETURN_CODE`` 77.
"""

import subprocess
import sys


def _warning_blocks(text):
    blocks = []
    current = None
    for line in text.splitlines():
        if line.startswith("CMake Warning"):
            if current:
                blocks.append(current)
            current = [line]
            continue
        if current is None:
            continue
        if line.strip() == "":
            blocks.append(current)
            current = None
            continue
        current.append(line)
    if current:
        blocks.append(current)
    return blocks


def voluntary_skip(text):
    for block in _warning_blocks(text):
        body = " ".join(line.strip() for line in block)
        if "skipping" in body and "missing" in body:
            return True
    return False


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: ctest_skip_wrapper.py COMMAND [ARG...]\n")
        return 2
    proc = subprocess.run(
        sys.argv[1:],
        capture_output=True,
        text=True,
        errors="replace",
    )
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    combined = proc.stdout + "\n" + proc.stderr
    if proc.returncode == 0 and voluntary_skip(combined):
        return 77
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
