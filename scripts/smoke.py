#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "c26.elf"
DISK = ROOT / "build" / "c26-smoke.img"
FIRST_BOOT_MARKERS = [
    "C26 RISC-V HOME COMPUTER",
    "INTERRUPTS: CLINT timer + PLIC online, idle uses WFI",
    "VIRTIO BLOCK: 4096 sectors online",
    "C26FS: formatted new disk",
    "C26FS: mounted 0 file(s)",
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
    "PRINT LET DEVICE READ DEVICE WRITE PEEK POKE LIST RUN NEW DIR SAVE LOAD HELP",
    "DEVICE READ returned 99",
    "SAVED BOOT",
    "ROBOT SDK DEMO",
    "stateful I2C + CAN control path ready",
    "C26 DEMO COMPLETE",
    "INTERRUPT ACTIVITY: timer=",
    "C26 INTERACTIVE LOOP ONLINE",
]
SECOND_BOOT_MARKERS = [
    "C26FS: mounted 1 file(s)",
    "LOADED BOOT",
    '10 PRINT "PERSISTED ACROSS BOOT"',
    "PERSISTED ACROSS BOOT",
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


def qemu_command() -> list[str]:
    return [
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
        "-drive",
        f"if=none,format=raw,file={DISK},id=c26disk",
        "-device",
        "virtio-blk-device,drive=c26disk",
    ]


def boot(input_text: str) -> str:
    try:
        result = run(qemu_command(), timeout=8.0, input_text=input_text)
        return (result.stdout or "") + (result.stderr or "")
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return stdout + stderr


def require_markers(output: str, markers: list[str], boot_name: str) -> bool:
    missing = [marker for marker in markers if marker not in output]
    if not missing:
        return True
    sys.stderr.write(output)
    sys.stderr.write(f"\nmissing {boot_name} c26 smoke markers: {', '.join(missing)}\n")
    return False


def main() -> int:
    build = run(["make", "build"])
    if build.returncode != 0:
        sys.stderr.write(build.stdout)
        sys.stderr.write(build.stderr)
        return build.returncode

    DISK.unlink(missing_ok=True)
    disk = run(["python3", "scripts/mkdisk.py", str(DISK)])
    if disk.returncode != 0:
        sys.stderr.write(disk.stdout)
        sys.stderr.write(disk.stderr)
        return disk.returncode

    first_output = boot(
        "help\n"
        "device write 128 99\n"
        "device read 128\n"
        "new\n"
        '10 print "old version"\n'
        "save boot\n"
        '10 print "persisted across boot"\n'
        "save boot\n"
        "dir\n"
    )
    if not require_markers(first_output, FIRST_BOOT_MARKERS, "first-boot"):
        return 1
    if first_output.count("SAVED BOOT") < 2:
        sys.stderr.write(first_output)
        sys.stderr.write("\nfirst boot did not prove file overwrite\n")
        return 1
    activity = re.search(r"INTERRUPT ACTIVITY: timer=(\d+) external=(\d+)",
                         first_output)
    if activity is None or int(activity.group(1)) == 0 or int(activity.group(2)) == 0:
        sys.stderr.write(first_output)
        sys.stderr.write("\nCLINT/PLIC interrupt counters did not advance\n")
        return 1

    second_output = boot("load boot\nlist\nrun\n")
    if not require_markers(second_output, SECOND_BOOT_MARKERS, "second-boot"):
        return 1

    print("c26 smoke passed (fresh format + persisted BASIC program across restart)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
