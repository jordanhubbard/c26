#!/usr/bin/env python3
from __future__ import annotations

import os
import socket
import subprocess
import sys
import re
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build" / "c26.elf"
DISK = ROOT / "build" / "c26-smoke.img"
TCP_PEER = ROOT / "scripts" / "tcp_peer.py"
UDP_PORT = 12000 + (os.getpid() % 3000)
NET_APP_PORT = UDP_PORT + 1
udp_echo_reply = b""
net_app_reply = b""

FIRST_BOOT_MARKERS = [
    "C26 RISC-V HOME COMPUTER",
    "INTERRUPTS: CLINT timer + PLIC online, idle uses WFI",
    "VIRTIO BLOCK: 16384 sectors online",
    "C26FS: formatted new disk",
    "C26FS: mounted 0 file(s)",
    "FRAMEBUFFER: virtio-gpu scanout 640x480x32",
    "VIRTIO INPUT: 2 device(s) online",
    "VIRTIO NET: online 10.0.2.15",
    "UDP ECHO SERVICE: port 2600",
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
    "MACHINE: COMMODORE",  # string var assign, concat, PRINT
    "AFFIRMATIVE",         # string INPUT + string IF comparison
    # Real networking: DNS resolves a dotted-quad, and the kernel TCP client
    # completes a full handshake / send / recv / close against the scripted
    # host peer reached through QEMU guestfwd (real TCP, deterministic).
    "RESOLVED 10.0.2.2",
    "TCP CONNECTED",
    "TCP SENT 9",
    "TCP RECV C26-TCP-OK HELLO-C26",
    "TCP CLOSED",
    # c26 Scheme REPL: compute, define, higher-order load, and an escape
    # continuation — the built-in Lisp coexisting with BASIC.
    "C26 SCHEME - LISP REPL",
    "SCM-ADD 42",
    "SCM-SQ 81",
    "squares: (36 25 16 9 4 1)",  # DEMO.SCM higher-order functions
    "scheme demo complete",
    "SCM-CC 70001",               # call/cc non-local exit (k 70000)
    "4243",                       # back in BASIC after exit
    "?ILLEGAL QUANTITY ERROR",  # SOUND rejects voice 9
    "DEVICE WRITE OK",
    "DEVICE READ returned 99",
    "SAVED BOOT",
    "SAVED TEMP",
    "RENAMED TEMP TO TEMP2",
    "DELETED TEMP2",
]

SECOND_BOOT_MARKERS = [
    # DEMO + HELLO.ASM + BOOT written by the guest, twelve cartridges
    # installed host-side, HI assembled on the machine.
    "C26FS: mounted 16 file(s)",
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
    # Multiprocessing: TICKER keeps running while the console answers.
    "TICKER CART ONLINE",
    "TICK",
    "JOB 0  TICKER",
    "41001",
    "51001",
    # Windows + IPC: PONG's window changes the composited framebuffer
    # (checked via FB below) and answers PING across address spaces.
    "PONG CART ONLINE",
    "PING CART ONLINE",
    "IPC ROUNDTRIP OK FROM JOB 0",
    "71001",
    # WM: a window resizes / minimizes / restores / closes, scripted from
    # BASIC; the job is killed by WINDOW CLOSE and the console keeps going.
    "81001",
    # The M6 suite: the NET app ACKs a real datagram from U-mode, the
    # tracker and the game come up and exit cleanly.
    "NET CART ONLINE",
    "NET RX HI-C26",
    "NET CART EXIT",
    "TRACKER CART ONLINE",
    "TRACKER CART EXIT",
    "BREAKOUT CART ONLINE",
    "BREAKOUT CART EXIT",
    # M7 self-hosting: the on-board assembler turns HELLO.ASM into a
    # cartridge, and RUN executes the machine-assembled code.
    "ASM CART ONLINE",
    "ASSEMBLED HELLO.ASM",
    "HELLO FROM SELF-HOSTED CODE",
    # M7 scriptable desktop: BASIC SEND delivers a message to a running
    # job across address spaces.
    "PONG GOT PING",
    # The M5 apps: EDIT saves a file typed through the toolkit; FILES lists
    # it, and its R action exercises the spawn syscall (DEMO is a BASIC
    # file, so the launcher's error path answers).
    "EDIT CART ONLINE",
    "SAVED NOTES",
    "NOTES",
    "FILES CART ONLINE",
    "FILES SEES",
    "Error: not a c26 cartridge",
    # Shared clipboard, both directions across the app boundary.
    "PASTE COPYME",   # BASIC round-trip through the kernel clipboard
    "EDIT PASTE 5",   # cart clip_get read what BASIC set ("PIECE")
    "EDIT COPY 5",    # cart clip_set wrote the current line
    "PASTE PIECE",    # BASIC read what EDIT copied
    "92002",
    "SHUTTING DOWN - GOODBYE",
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
new
10 a$ = "commo"
20 b$ = a$ + "dore"
30 print "machine: ";b$
40 input n$
50 if n$ = "yes" then print "affirmative"
run
yes
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
print "clock ";time
resolve "10.0.2.2"
tcp 10,0,2,100,80
tcp send "HELLO-C26"
tcp recv
tcp close
scheme
(display "SCM-ADD ")(display (+ 40 2))(newline)
(define (sq x) (* x x))
(display "SCM-SQ ")(display (sq 9))(newline)
(load "DEMO.SCM")
(display "SCM-CC ")(display (+ 1 (call/cc (lambda (k) (k 70000) 999))))(newline)
exit
print 4242+1
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
        "-netdev",
        # UDP echo/NET-app hostfwds, plus a guestfwd that hands any guest TCP
        # connection to 10.0.2.100:80 to a scripted host peer — QEMU's own TCP
        # terminates the guest side, so the kernel TCP client is exercised
        # against a real, deterministic peer with no external network.
        f"user,id=net0,hostfwd=udp:127.0.0.1:{UDP_PORT}-:2600,"
        f"hostfwd=udp:127.0.0.1:{NET_APP_PORT}-:2601,"
        f"guestfwd=tcp:10.0.2.100:80-cmd:{sys.executable} {TCP_PEER}",
        "-device",
        "virtio-net-device,netdev=net0",
    ]


def boot(input_text: str, timeout: float) -> str:
    try:
        result = run(qemu_command(), timeout=timeout, input_text=input_text)
        return (result.stdout or "") + (result.stderr or "")
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        return stdout + stderr


def udp_probe(port: int, payload: bytes) -> bytes:
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.settimeout(2.0)
    reply = b""
    for _ in range(4):
        try:
            probe.sendto(payload, ("127.0.0.1", port))
            reply, _ = probe.recvfrom(80)
            break
        except OSError:
            continue
    probe.close()
    return reply


def udp_echo_probe() -> None:
    """The kernel echo service answers a datagram through hostfwd."""
    global udp_echo_reply
    udp_echo_reply = udp_probe(UDP_PORT, b"C26-NET-PING")


def net_app_probe() -> None:
    """The NET cartridge (U-mode, via udp syscalls) ACKs a datagram."""
    global net_app_reply
    net_app_reply = udp_probe(NET_APP_PORT, b"HI-C26")


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
    ('\x03print 999-111\n', 8.0),
    ('run "ticker"\n', 3.0),
    ('\x14jobs\nprint 41000+1\n', 4.0),
    ('kill 0\nprint 51000+1\n', 4.0),
    ('print fb\nrun "pong"\n', 3.0),
    ('\x14print fb\nrun "ping"\n', 4.0),
    ('\x14kill 0\nprint 71000+1\n', 4.0),
    # Window management, scripted from BASIC so the affordances are gated
    # headlessly: a running cart's window resizes, minimizes, restores, and
    # closes — each mutating the composited framebuffer (WM checksums below).
    ('run "pong"\n', 3.0),
    ('\x14print "WM";fb\n', 2.0),        # baseline: window floats over console
    ('window size 0,240,160\n', 2.0),
    ('print "WM";fb\n', 2.0),            # resized -> checksum changes
    ('window min 0\n', 2.0),
    ('print "WM";fb\n', 2.0),            # minimized to the title bar
    ('window max 0\n', 2.0),
    ('print "WM";fb\n', 2.0),            # restored
    ('window close 0\nprint 81000+1\n', 3.0),  # close kills the job
    (udp_echo_probe, 1.0),
    ('run "net"\n', 3.0),
    (net_app_probe, 1.0),
    ('q', 2.0),
    ('run "tracker"\n', 3.0),
    (' ', 1.5),                # play the pattern for a beat
    ('q', 2.0),
    ('run "breakout"\n', 3.0),
    ('q', 2.0),
    ('run asm\n', 3.0),
    ('hello.asm hi\n', 3.0),   # assemble the seeded source into a cartridge
    ('q', 2.0),
    ('run hi\n', 3.0),         # run the machine-assembled cartridge
    ('run pong\n', 2.0),
    ('\x14send 0,"PING"\n', 3.0),  # script a running job from BASIC
    ('kill 0\n', 2.0),
    ('run "edit"\n', 3.0),
    ('SMOKE NOTE\x13', 2.0),   # type into EDIT, Ctrl-S saves
    ('\x11', 2.0),             # Ctrl-Q quits the editor
    ('dir\nrun "files"\n', 3.0),
    ('r', 2.0),                # R on the first entry (DEMO): spawn error path
    ('q', 2.0),                # quit FILES
    # Shared clipboard: a BASIC round-trip, then copy/paste crossing the app
    # boundary in both directions — BASIC sets the clipboard and EDIT pastes
    # it (Ctrl-Y, clip_get syscall), then EDIT copies a line (Ctrl-W,
    # clip_set syscall) and BASIC pastes what the app copied.
    ('clip "COPYME"\n', 2.0),
    ('paste\n', 2.0),              # PASTE COPYME (BASIC set -> BASIC get)
    ('delete notes\nclip "PIECE"\nrun "edit"\n', 3.0),
    ('\x19', 2.0),                 # Ctrl-Y: EDIT reads BASIC's clip -> EDIT PASTE 5
    ('\x17', 2.0),                 # Ctrl-W: EDIT copies the line -> EDIT COPY 5
    ('\x11', 2.0),                 # Ctrl-Q quits the editor
    ('\x14paste\n', 2.0),          # PASTE PIECE (BASIC reads EDIT's clip)
    ('print 92000+2\nbye\n', 4.0),  # the machine powers itself off
]


def boot_staged(stages) -> str:
    process = subprocess.Popen(
        qemu_command(),
        cwd=str(ROOT),
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        for action, delay in stages:
            if callable(action):
                action()
            else:
                process.stdin.write(action)
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
    # The goldfish RTC must report a real wall-clock (after 2023-11).
    clock = re.search(r"CLOCK (\d+)", first_output)
    if clock is None or int(clock.group(1)) < 1700000000:
        failures.append(f"RTC did not report a real time: {clock and clock.group(1)}")
    if failures:
        sys.stderr.write(first_output)
        sys.stderr.write("\n" + "\n".join(failures) + "\n")
        return 1

    install = run(["python3", "scripts/fsinstall.py", str(DISK),
                   "PAINT=build/paint.cart", "CRASH=build/crash.cart",
                   "SPIN=build/spin.cart", "TICKER=build/ticker.cart",
                   "PING=build/ping.cart", "PONG=build/pong.cart",
                   "FILES=build/files.cart", "EDIT=build/edit.cart",
                   "TRACKER=build/tracker.cart",
                   "BREAKOUT=build/breakout.cart", "NET=build/net.cart",
                   "ASM=build/asm.cart"])
    if install.returncode != 0:
        sys.stderr.write(install.stdout)
        sys.stderr.write(install.stderr)
        sys.stderr.write("\nfsinstall could not modify the guest-formatted disk\n")
        return 1

    second_output = boot_staged(SECOND_BOOT_STAGES)
    if not require_markers(second_output, SECOND_BOOT_MARKERS, "second-boot"):
        return 1
    # Concurrency proof: TICKER must still be printing after the console
    # answered 41001 — background output interleaved with foreground work.
    if second_output.rindex("TICK") <= second_output.index("41001"):
        sys.stderr.write(second_output)
        sys.stderr.write("\nno TICK after console interaction — "
                         "background job did not run concurrently\n")
        return 1
    # Windowing proof: the composited framebuffer changes once PONG's
    # window floats over the console.
    window_sums = re.findall(r"PRINT FB\r?\n(\d+)", second_output)
    if len(window_sums) < 2 or window_sums[-2] == window_sums[-1]:
        sys.stderr.write(second_output)
        sys.stderr.write("\nframebuffer unchanged by a window — "
                         "compositor did not draw it\n")
        return 1
    # Window management proof: baseline, resize, minimize, restore — every
    # window operation must visibly change the composited framebuffer, and
    # the close must terminate the job.
    wm_sums = re.findall(r"WM(\d+)", second_output)
    if len(wm_sums) != 4 or "0" in wm_sums:
        failures2 = f"expected 4 nonzero WM checksums, got {wm_sums}"
        sys.stderr.write(second_output)
        sys.stderr.write("\n" + failures2 + "\n")
        return 1
    if any(wm_sums[i] == wm_sums[i + 1] for i in range(3)):
        sys.stderr.write(second_output)
        sys.stderr.write(f"\na window op did not change the framebuffer: "
                         f"{wm_sums}\n")
        return 1
    # Networking proof: a real UDP datagram went from the host through
    # QEMU's user network into the guest stack and came back.
    if udp_echo_reply != b"C26-NET-PING":
        sys.stderr.write(second_output)
        sys.stderr.write(f"\nUDP echo failed: got {udp_echo_reply!r}\n")
        return 1
    if net_app_reply != b"ACK:HI-C26":
        sys.stderr.write(second_output)
        sys.stderr.write(f"\nNET app ACK failed: got {net_app_reply!r}\n")
        return 1

    print("c26 smoke passed: BASIC + a built-in Scheme REPL (call/cc, a "
          "higher-order DEMO.SCM driving the desktop), graphics, sound, "
          "C26FS v2, two-boot persistence, multiprocessing U-mode cartridges "
          "(contained fault, preemptive kill, concurrent job), windows + "
          "IPC with resize/minimize/close window management and a shared "
          "clipboard (copy/paste across apps), FILES/EDIT with "
          "spawn, a real UDP round trip, a kernel TCP "
          "client handshaking with a scripted host peer over guestfwd, DNS "
          "resolution, and a guest-initiated power-off")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
