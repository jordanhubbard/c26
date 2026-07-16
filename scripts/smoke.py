#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import re
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "c26.elf"
DISK = ROOT / "build" / "c26-smoke.img"

FIRST_BOOT_MARKERS = [
    "C26 RISC-V HOME COMPUTER",
    "INTERRUPTS: CLINT timer + PLIC online, idle uses WFI",
    "VIRTIO BLOCK: 16384 sectors online",
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
    "ROBOT SDK DEMO",
    "stateful I2C + CAN control path ready",
    "C26 HARDWARE ONLINE",
    "C26 INTERACTIVE LOOP ONLINE",
    "C26 BASIC V3.0",
    "C26FS: DEMO program installed",
    "READY",
    # Interactive language proof, fed over the serial console:
    "SCREEN CLS COLOR PLOT LINE RECT TEXT SOUND DEVICE PEEK POKE ROBOT",
    "100014",            # PRINT 100000+(3+4)*2  (precedence + parens)
    "-10",               # PRINT 10-20           (signed arithmetic)
    "LOOPS DONE",        # FOR/NEXT completed
    "IN SUBROUTINE",     # GOSUB reached
    "AFTER GOSUB",       # RETURN resumed
    "1084",              # INPUT round-trip: 42*2+1000
    "COMPARE OK",        # IF true branch
    "IF DONE",           # IF false branch skipped, program continued
    "?ILLEGAL QUANTITY ERROR",  # SOUND rejects voice 9
    "DEVICE WRITE OK",
    "DEVICE READ returned 99",
    "SAVED BOOT",
    "SAVED TEMP",
    "RENAMED TEMP TO TEMP2",
    "DELETED TEMP2",
]

SECOND_BOOT_MARKERS = [
    # DEMO + BOOT written by the guest, PAINT/CRASH/SPIN installed host-side
    # between boots — fsinstall.py modifying a guest-formatted filesystem.
    "C26FS: mounted 5 file(s)",
    "LOADED BOOT",
    '10 PRINT "PERSISTED ACROSS BOOT"',
    "PERSISTED ACROSS BOOT",
    "LOADED DEMO",
    "SELF DEMO COMPLETE",
    "PAINT",
    "CART START PAINT",
    "PAINT CART ONLINE",
    "PAINT CART EXIT",
    "CART EXIT 0",
    # Protection: a wild write to kernel memory faults and is contained...
    "CRASH CART ONLINE",
    "CART FAULT cause=",
    "CART EXIT -1",
    "333",
    # ...and a hung cartridge is preemptively killed by Ctrl-C. After each,
    # the machine keeps answering.
    "SPIN CART ONLINE",
    "CART KILLED",
    "CART EXIT -3",
    "888",
]

FIRST_BOOT_INPUT = """\
help
print 100000+(3+4)*2
print 10-20
new
10 for i=1 to 3
20 print i*i
30 next
40 print "loops done"
run
new
10 gosub 100
20 print "after gosub"
30 end
100 print "in subroutine"
110 return
run
new
10 input a
20 print a*2+1000
run
42
new
10 if 2>1 then print "compare ok"
20 if 1>2 then print "bad branch"
30 print "if done"
run
screen 1
plot 10,10
print fb
rect 20,20,100,80
print fb
screen 0
sound 0,440
sound 9,440
sound 0,0
device write 128 99
device read 128
new
10 print "old version"
save boot
new
10 print "persisted across boot"
save boot
save temp
rename temp,temp2
delete temp2
dir
"""

# Ctrl-C is a real-time signal, not a queued character: it kills whichever
# cartridge is running when it arrives. The second boot therefore feeds its
# input in stages, sending \x03 only once SPIN is definitely running.
SECOND_BOOT_STAGES = [
    (
        'load boot\n'
        'list\n'
        'run\n'
        'load demo\n'
        'run\n'
        'dir\n'
        'run "paint"\n'
        'q\n'
        'run "crash"\n'
        'print 111+222\n'
        'run "spin"\n',
        30.0,
    ),
    ('\x03print 999-111\n', 10.0),
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


def boot(input_text: str, timeout: float) -> str:
    try:
        result = run(qemu_command(), timeout=timeout, input_text=input_text)
        return (result.stdout or "") + (result.stderr or "")
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return stdout + stderr


def boot_staged(stages: list[tuple[str, float]]) -> str:
    process = subprocess.Popen(
        qemu_command(),
        cwd=str(ROOT),
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        for text, delay in stages:
            process.stdin.write(text)
            process.stdin.flush()
            time.sleep(delay)
    except BrokenPipeError:
        pass
    process.kill()
    output = process.stdout.read() if process.stdout else ""
    process.wait()
    return output


def require_markers(output: str, markers: list[str], boot_name: str) -> bool:
    missing = [marker for marker in markers if marker not in output]
    if not missing:
        return True
    sys.stderr.write(output)
    sys.stderr.write(f"\nmissing {boot_name} c26 smoke markers: {', '.join(missing)}\n")
    return False


def main() -> int:
    build = run(["make", "build", "carts"])
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

    first_output = boot(FIRST_BOOT_INPUT, timeout=60.0)
    if not require_markers(first_output, FIRST_BOOT_MARKERS, "first-boot"):
        return 1
    failures: list[str] = []
    if first_output.count("SAVED BOOT") < 2:
        failures.append("first boot did not prove file overwrite")
    # FOR/NEXT must produce the squares in order.
    if re.search(r"1\r?\n4\r?\n9\r?\nLOOPS DONE", first_output) is None:
        failures.append("FOR/NEXT did not print 1,4,9 in order")
    # The false IF branch must not execute: its text appears only as the echo.
    if first_output.count("BAD BRANCH") != 1:
        failures.append("IF false branch executed")
    # Graphics statements must actually change framebuffer contents.
    checksums = re.findall(r"PRINT FB\r?\n(\d+)", first_output)
    if len(checksums) != 2 or checksums[0] == checksums[1] or "0" in checksums:
        failures.append(f"framebuffer checksums did not change: {checksums}")
    activity = re.search(r"INTERRUPT ACTIVITY: timer=(\d+) external=(\d+)",
                         first_output)
    if activity is None or int(activity.group(1)) == 0 or int(activity.group(2)) == 0:
        failures.append("CLINT/PLIC interrupt counters did not advance")
    if failures:
        sys.stderr.write(first_output)
        sys.stderr.write("\n" + "\n".join(failures) + "\n")
        return 1

    install = run(["python3", "scripts/fsinstall.py", str(DISK),
                   "PAINT=build/paint.cart", "CRASH=build/crash.cart",
                   "SPIN=build/spin.cart"])
    if install.returncode != 0:
        sys.stderr.write(install.stdout)
        sys.stderr.write(install.stderr)
        sys.stderr.write("\nfsinstall could not modify the guest-formatted disk\n")
        return 1

    second_output = boot_staged(SECOND_BOOT_STAGES)
    if not require_markers(second_output, SECOND_BOOT_MARKERS, "second-boot"):
        return 1

    print("c26 smoke passed: language, graphics, sound, C26FS v2, two-boot "
          "persistence, and U-mode cartridges — clean run, contained kernel-"
          "memory fault, and preemptive kill of a hung app")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
