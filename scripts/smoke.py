#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "c26.elf"
MARKERS = [
    "C26 RISC-V HOME COMPUTER",
    "FRAMEBUFFER: virtio-gpu scanout 640x480x32",
    "VIRTIO INPUT: 2 device(s) online",
    "C26 DESKTOP: graphical shell online",
    "OPENGL-STYLE SDK: z-buffered triangle rasterizer online",
    "RAY TRACER: two shaded spheres rendered",
    "AUDIO MIXER: 8 voices, 48kHz stereo PCM, pan + envelope",
    "VIRTIO SOUND: PCM output stream online",
    "DEVICE SDK: register readback=42",
    "CAN SDK: loopback frame received",
    "TCP/IP SDK: packet loopback received",
    "BASIC READY",
    "HELLO FROM C26",
    "DEVICE READ returned 26",
    "PRINT LET DEVICE READ DEVICE WRITE PEEK POKE HELP",
    "DEVICE READ returned 99",
    "ROBOT SDK DEMO",
    "stateful I2C + CAN control path ready",
    "C26 DEMO COMPLETE",
    "C26 INTERACTIVE LOOP ONLINE",
]


def run(
    command: list[str],
    *,
    timeout: float | None = None,
    input_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
        input=input_text,
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
        "-global",
        "virtio-mmio.force-legacy=false",
        "-cpu",
        "rv64",
        "-m",
        "256M",
        "-display",
        "none",
        "-serial",
        "stdio",
        "-monitor",
        "none",
        "-bios",
        "none",
        "-no-reboot",
        "-kernel",
        str(ELF),
        "-device",
        "virtio-gpu-device",
        "-device",
        "virtio-keyboard-device",
        "-device",
        "virtio-mouse-device",
        "-audiodev",
        "driver=none,id=audio0",
        "-device",
        "virtio-sound-device,audiodev=audio0",
    ]
    try:
        result = run(
            qemu_command,
            timeout=6.0,
            input_text="help\ndevice write 128 99\ndevice read 128\n",
        )
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
