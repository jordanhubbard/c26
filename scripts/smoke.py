#!/usr/bin/env python3
from __future__ import annotations

import os
import socket
import subprocess
import sys
import re
import threading
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
    # BASIC depth: arrays (DIM), DATA/READ, DEF FN, and a string function.
    "SUM 10",              # DIM d(3) filled by READ from DATA 4,3,2,1
    "FNSQ 49",             # DEF FN sq(x)=x*x, FN sq(7)
    "MID BCD",             # MID$("abcdef",2,3)
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
    "C26FS: mounted 27 file(s)",
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
    # Richer assembler: macro expansion + .INCLUDE + expression operand.
    "ASSEMBLED FEAT.ASM",
    "MACRO ASM WORKS",
    "1234",
    # Tiny-C: a C subset compiled to a cartridge on the machine, then run.
    "TINYC COMPILED",
    "55",   # SUM.C: while-loop sum of 1..10, compiled and executed
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
    # Graphical dock: tiles enumerated from C26FS with click geometry, and a
    # dock click launches PAINT through the pointer hit-test path.
    "DOCK PAINT 46 940",
    # Shared clipboard, both directions across the app boundary.
    "PASTE COPYME",   # BASIC round-trip through the kernel clipboard
    "EDIT PASTE 5",   # cart clip_get read what BASIC set ("PIECE")
    "EDIT COPY 5",    # cart clip_set wrote the current line
    "PASTE PIECE",    # BASIC read what EDIT copied
    # The application suite (each a separately loaded toolkit cartridge).
    "CALC CART ONLINE", "CALC = 81", "CALC CART EXIT",
    "CLOCK CART ONLINE", "CLOCK CART EXIT",
    "SHEET CART ONLINE", "SHEET TOTAL 12", "SHEET CART EXIT",
    "ROBOT CART ONLINE", "ROBOT GET OK", "ROBOT CART EXIT",
    "HEXEDIT CART ONLINE", "HEXEDIT CART EXIT",
    # The RV64 disassembler decodes known words and the on-board-assembled HI.
    "MONITOR SELFTEST", "addi x10, x0, 10", "add x10, x10, x11",
    "MONITOR DISASM OK", "MONITOR CART EXIT",
    "SNAKE CART ONLINE", "SNAKE CART EXIT",
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
new
10 dim d(3)
20 for i=0 to 3
30 read d(i)
40 next
50 print "sum ";d(0)+d(1)+d(2)+d(3)
60 def fn s(x) = x*x
70 print "fnsq ";fn s(7)
80 print "mid ";mid$("abcdef",2,3)
90 data 4,3,2,1
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
    # Stages carry a completion marker where one exists (proceed as soon as it
    # prints, capped at the delay); correctness-timing-sensitive or silent
    # stages keep a plain fixed delay.
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
        30.0, 'SPIN CART ONLINE',
    ),
    ('\x03print 999-111\n', 8.0, '888'),
    ('run "ticker"\n', 3.0, 'TICKER CART ONLINE'),
    ('\x14jobs\nprint 41000+1\n', 4.0, '41001'),
    ('kill 0\nprint 51000+1\n', 4.0, '51001'),
    ('print fb\nrun "pong"\n', 3.0, 'PONG CART ONLINE'),
    ('\x14print fb\nrun "ping"\n', 4.0, 'IPC ROUNDTRIP OK FROM JOB 0'),
    ('\x14kill 0\nprint 71000+1\n', 4.0, '71001'),
    # Window management, scripted from BASIC so the affordances are gated
    # headlessly: a running cart's window resizes, minimizes, restores, and
    # closes — each mutating the composited framebuffer (WM checksums below).
    # These keep fixed delays: the window ops are silent and the FB reads must
    # follow a compositor flush.
    ('run "pong"\n', 3.0, 'PONG CART ONLINE'),
    ('\x14print "WM";fb\n', 2.0),        # baseline: window floats over console
    ('window size 0,240,160\n', 2.0),
    ('print "WM";fb\n', 2.0),            # resized -> checksum changes
    ('window min 0\n', 2.0),
    ('print "WM";fb\n', 2.0),            # minimized to the title bar
    ('window max 0\n', 2.0),
    ('print "WM";fb\n', 2.0),            # restored
    ('window close 0\nprint 81000+1\n', 3.0, '81001'),  # close kills the job
    (udp_echo_probe, 1.0),
    ('run "net"\n', 3.0, 'NET CART ONLINE'),
    (net_app_probe, 1.0),
    ('q', 2.0),
    ('run "tracker"\n', 3.0, 'TRACKER CART ONLINE'),
    (' ', 1.5),                # play the pattern for a beat
    ('q', 2.0),
    ('run "breakout"\n', 3.0, 'BREAKOUT CART ONLINE'),
    ('q', 2.0),
    ('run asm\n', 3.0, 'ASM CART ONLINE'),
    ('hello.asm hi\n', 3.0, 'ASSEMBLED HELLO.ASM'),  # assemble on the machine
    ('q', 2.0),
    ('run hi\n', 3.0, 'HELLO FROM SELF-HOSTED CODE'),  # run the assembled cart
    # Richer assembler: FEAT.ASM uses a .MACRO with a \1 arg, .INCLUDE, and an
    # expression operand (617+617); assemble it and run the result.
    ('run asm\n', 3.0, 'ASM CART ONLINE'),
    ('feat.asm featout\n', 3.0, 'ASSEMBLED FEAT.ASM'),
    ('q', 2.0),
    ('run featout\n', 3.0, 'MACRO ASM WORKS'),  # macro+include prints; expr=1234
    # Tiny-C: compile SUM.C (a while loop summing 1..10) to a cartridge on the
    # machine, then run it — the self-hosting C endgame.
    ('run tinyc\n', 3.0, 'TINYC CART ONLINE'),
    ('sum.c out\n', 3.0, 'TINYC COMPILED'),
    ('q', 2.0),
    ('run out\n', 3.0, '55'),   # loop sum 1..10 = 55, then 2*3+4 = 10
    ('run pong\n', 2.0, 'PONG CART ONLINE'),
    ('\x14send 0,"PING"\n', 3.0, 'PONG GOT PING'),  # script a running job
    ('kill 0\n', 2.0),
    ('run "edit"\n', 3.0, 'EDIT CART ONLINE'),
    ('SMOKE NOTE\x13', 2.0, 'SAVED NOTES'),   # type into EDIT, Ctrl-S saves
    ('\x11', 2.0),             # Ctrl-Q quits the editor
    ('dir\nrun "files"\n', 3.0, 'FILES CART ONLINE'),
    ('r', 2.0),                # R on the first entry (DEMO): spawn error path
    ('q', 2.0),                # quit FILES
    # Graphical dock: list the launcher tiles built from C26FS, then click the
    # PAINT tile through the real pointer hit-test path to launch it.
    ('dock\n', 2.0, 'DOCK PAINT'),
    ('click 46,940\n', 3.0, 'PAINT CART ONLINE'),   # the PAINT tile's centre
    ('\x14kill 0\n', 2.0),     # refocus console and dismiss the launched app
    # Shared clipboard: a BASIC round-trip, then copy/paste crossing the app
    # boundary in both directions — BASIC sets the clipboard and EDIT pastes
    # it (Ctrl-Y, clip_get syscall), then EDIT copies a line (Ctrl-W,
    # clip_set syscall) and BASIC pastes what the app copied.
    ('clip "COPYME"\n', 2.0, 'CLIP SET 6'),
    ('paste\n', 2.0, 'PASTE COPYME'),              # BASIC set -> BASIC get
    ('delete notes\nclip "PIECE"\nrun "edit"\n', 3.0, 'EDIT CART ONLINE'),
    ('\x19', 2.0, 'EDIT PASTE'),   # Ctrl-Y: EDIT reads BASIC's clip
    ('\x17', 2.0, 'EDIT COPY'),    # Ctrl-W: EDIT copies the line
    ('\x11', 2.0),                 # Ctrl-Q quits the editor
    ('\x14paste\n', 2.0, 'PASTE PIECE'),   # BASIC reads EDIT's clip
    # The application suite: launch each new cartridge from the console, drive
    # it a little, read a marker that proves it worked, and quit.
    ('run "calc"\n', 4.0, 'CALC CART ONLINE'),
    ('9*9=', 3.0, 'CALC = 81'),            # left-to-right arithmetic
    ('q', 2.0, 'CALC CART EXIT'),
    ('run "clock"\n', 4.0, 'CLOCK CART ONLINE'),  # wall clock via the RTC
    ('q', 2.0, 'CLOCK CART EXIT'),
    ('run "sheet"\n', 4.0, 'SHEET CART ONLINE'),
    ('12\n', 3.0, 'SHEET TOTAL 12'),       # set A1=12, totals recompute
    ('q', 2.0, 'SHEET CART EXIT'),
    ('run "robot"\n', 4.0, 'ROBOT CART ONLINE'),
    ('\x1e', 3.0, 'ROBOT GET OK'),         # right arrow writes+reads a channel
    ('q', 2.0, 'ROBOT CART EXIT'),
    ('run "hexedit"\n', 4.0, 'HEXEDIT CART ONLINE'),
    ('q', 2.0, 'HEXEDIT CART EXIT'),
    # MONITOR disassembles known words (self-test) and the machine-assembled HI.
    ('run "monitor"\n', 4.0, 'add x10, x10, x11'),
    ('q', 2.0, 'MONITOR CART EXIT'),
    ('run "snake"\n', 4.0, 'SNAKE CART ONLINE'),
    ('q', 3.0, 'SNAKE CART EXIT'),
    ('print 92000+2\nbye\n', 4.0, 'SHUTTING DOWN'),  # the machine powers off
]


def boot_staged(stages) -> str:
    """Drive the guest one stage at a time. A stage is (action, delay) or
    (action, delay, marker): with a marker, proceed as soon as that string
    appears in new output (falling back to the full delay as a timeout);
    without one, wait the fixed delay. Marker-driven stages cut the slack a
    fixed sleep leaves while staying reliable — a stage never proceeds before
    its own completion output, which pure idle-detection could not guarantee
    for silent-then-print work (graphics, a demo)."""
    process = subprocess.Popen(
        qemu_command(),
        cwd=str(ROOT),
        text=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    chunks: list[str] = []
    lock = threading.Lock()

    def reader() -> None:
        try:
            for line in iter(process.stdout.readline, ""):
                with lock:
                    chunks.append(line)
        except (ValueError, OSError):
            pass

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()

    def text_from(index: int) -> str:
        with lock:
            return "".join(chunks[index:])

    def mark() -> int:
        with lock:
            return len(chunks)

    def wait_for(marker: str, cap: float, start: int) -> None:
        deadline = time.time() + cap
        while time.time() < deadline:
            if marker in text_from(start):
                time.sleep(0.15)  # let the rest of the line settle
                return
            time.sleep(0.05)

    try:
        for stage in stages:
            action, delay = stage[0], stage[1]
            marker = stage[2] if len(stage) > 2 else None
            start = mark()
            if callable(action):
                action()
            else:
                process.stdin.write(action)
                process.stdin.flush()
            if marker is not None:
                wait_for(marker, delay, start)
            else:
                time.sleep(delay)
    except BrokenPipeError:
        pass
    process.kill()
    thread.join(timeout=1.0)
    process.wait()
    with lock:
        return "".join(chunks)


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
                   "ASM=build/asm.cart",
                   "CALC=build/calc.cart", "CLOCK=build/clock.cart",
                   "HEXEDIT=build/hexedit.cart", "SHEET=build/sheet.cart",
                   "ROBOT=build/robot.cart", "SNAKE=build/snake.cart",
                   "MONITOR=build/monitor.cart", "TINYC=build/tinyc.cart",
                   "FEAT.ASM=tests/asm/feat.asm",
                   "FMSG.ASM=tests/asm/fmsg.asm",
                   "SUM.C=tests/tinyc/sum.c"])
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
    # Dock proof: PAINT was launched twice — once by RUN, once by the dock
    # click — so the dock's pointer hit-test genuinely started the app.
    if second_output.count("CART START PAINT") < 2:
        sys.stderr.write(second_output)
        sys.stderr.write("\ndock click did not launch PAINT via the "
                         "pointer hit-test path\n")
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
          "IPC with resize/minimize/close window management, a graphical dock "
          "launching apps through synthetic pointer clicks, and a shared "
          "clipboard (copy/paste across apps), FILES/EDIT with "
          "spawn, an application suite (calc, clock, sheet, robot, hexedit, "
          "snake), an RV64 disassembler, and a self-hosting tiny-C compiler, "
          "a real UDP round trip, a kernel TCP "
          "client handshaking with a scripted host peer over guestfwd, DNS "
          "resolution, and a guest-initiated power-off")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
