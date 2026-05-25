#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "c26.elf"
MARKERS = [
    "C26 RISC-V HOME COMPUTER",
    "C26 DESKTOP",
    "BASIC READY",
    "HELLO FROM C26",
    "26",
    "42",
    "PEEK returned emulated device byte 26",
    "C26 DEMO COMPLETE",
]


def run(command: list[str], *, timeout: float | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def main() -> int:
    build = run(["make", "build"])
    if build.returncode != 0:
        sys.stderr.write(build.stdout)
        sys.stderr.write(build.stderr)
        return build.returncode

    qemu_command = [
        "qemu-system-riscv64",
        "-M",
        "virt",
        "-cpu",
        "rv64",
        "-m",
        "256M",
        "-nographic",
        "-bios",
        "none",
        "-no-reboot",
        "-kernel",
        str(ELF),
    ]
    try:
        result = run(qemu_command, timeout=3.0)
        output = (result.stdout or "") + (result.stderr or "")
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        output = stdout + stderr

    missing = [marker for marker in MARKERS if marker not in output]
    if missing:
        sys.stderr.write(output)
        sys.stderr.write("\nmissing c26 smoke markers: %s\n" % ", ".join(missing))
        return 1
    print("c26 smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
