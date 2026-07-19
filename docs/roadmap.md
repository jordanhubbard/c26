# c26 Roadmap

The first-principles goal: a home computer is a tight loop — you type on the
machine, the machine's own screen answers, and a few lines of the built-in
language make the hardware do something visible and audible. The 2026-07
"interactive machine" milestone closed that loop:

1. Full printable-ASCII font and a scrolling framebuffer text console; all
   system and BASIC output renders on the machine's own display and mirrors
   to UART for automation.
2. Screen modes and app lifecycle: boot to BASIC, Esc to the desktop
   launcher, `SCREEN 1` graphics mode.
3. BASIC V3: real expressions, control flow, `INPUT`/`GET`/`PAUSE`, and
   hardware statements wrapping the graphics, sound, and device SDKs.
4. A self demo: a fresh machine installs `DEMO` — written in BASIC, drawing
   and playing sound through the same statements users type.
5. Smoke v2 gates all of it headlessly, including the two-process disk
   persistence proof.

## Delivered: M2 — the cartridge port (2026-07-17)

C26FS v2 (64 files, 128 KiB, free-sector bitmap, DELETE/RENAME), the frozen
`c26_api.h` cartridge ABI, the loader, host-side installation via
`scripts/fsinstall.py`, and `apps/paint` as the first out-of-tree program.
Version 1 is cooperative and unprotected by design.

## Delivered: M3 — protection (2026-07-17)

Cartridges now run in U-mode inside per-app Sv39 address spaces built by
src/vm.c, reached only through the 25-syscall surface in c26_user.h (the
c26_api_t contract is unchanged — its pointers land in a user-mapped stub
page). The M-mode kernel keeps ownership on every interrupt: input and audio
survive a spinning app, Ctrl-C kills it, and a wild pointer faults the app
while the machine keeps answering. apps/crash and apps/spin prove both in
the smoke gate. Single-process by design at this stage.

## Delivered: M4a — multiprocessing + compositor core (2026-07-17)

Four concurrent U-mode processes in round-robin slices, each with its own
Sv39 space (same link VA, different physical slot) and its own 640x480
surface. The compositor shows the focused surface with a status bar; Tab /
Ctrl-T switch focus while everything keeps running; JOBS and KILL manage
jobs from BASIC; apps/ticker's heartbeat interleaving with console output is
the smoke gate's concurrency proof. Also: the full build harness (header
deps, compile_commands.json, host-side unit tests for basic.c and fs.c,
make check).

## Delivered: M4b — windows + IPC, ABI v2 (2026-07-17)

The console is the root layer; every process owns a movable, decorated
window composited in z-order — click to focus and raise, drag the title bar
to move, window-local mouse, window_size() layout (ABI v2). Bounded mailbox
IPC (send/recv) between jobs; apps/ping + apps/pong gate the round trip and
an FB checksum gates the compositing, headlessly.

## Delivered: M5 — UI toolkit + the first real apps (2026-07-17)

apps/lib/ui.*: a small immediate-mode toolkit (titlebar, rows, status,
click hit-testing, throttled present) linked into cartridges. FILES: browse
C26FS, launch cartridges via the new spawn syscall, delete, or open the
selection in EDIT with the filename handed over IPC. EDIT: a windowed text
editor (arrows via new getchar key codes, Ctrl-S/Ctrl-Q). BYE powers the
machine off through the SiFive test finisher; HALT is the immediate debug
variant. The smoke gate types into EDIT, saves, lists the file, exercises
FILES' spawn error path, and ends with a guest-initiated power-off.

## Delivered: M6a — honest networking (2026-07-17)

src/net.c: a virtio-net driver plus a deliberately small IPv4 stack — ARP
(request, reply, and learning the gateway MAC from forwarded traffic), ICMP
echo reply, and UDP with bounded port bindings. The kernel answers UDP echo
on port 2600; cartridges get udp_bind/udp_send/udp_recv syscalls. The smoke
gate sends a real datagram from the host through QEMU user-net hostfwd into
the guest stack and requires the echo — the old "TCP/IP loopback" label is
retired for real networking.

## Delivered: M6b — the application suite (2026-07-17)

TRACKER (8-voice step sequencer with pattern save), BREAKOUT (mouse
paddle, mixer blips, bricks), and NET (a UDP mailbox app that ACKs every
datagram — the smoke gate reaches it from the host through its U-mode udp
syscalls). apps/lib/rt.c supplies freestanding memset/memcpy for
cartridges. The experiment scorecard lives in docs/experiment.md.

## Delivered: M7 — self-hosting + scriptable desktop (2026-07-17)

apps/asm: a two-pass RV64 assembler that runs on the machine as a
protected cartridge, assembling a C26FS source file into a runnable
cartridge (RUN ASM -> "HELLO.ASM HI" -> RUN HI). A fresh disk seeds
HELLO.ASM so self-hosting demonstrates out of the box. BASIC gained
WINDOW/FOCUS/SEND so the window manager and IPC are scriptable from the
built-in language. C26FS filenames now allow '.'. The experiment's closing
verdict is in docs/experiment.md. **The charted course is complete.**

## Delivered: c26 Scheme — a Lisp built-in language (2026-07-19)

A second built-in language, reframing the machine toward the Lisp machine
as much as the C64. `src/scheme.c` is a small integer Scheme — reader,
printer, tagged values (immediate fixnums), a tree-walking evaluator with
proper tail calls, a conservative mark-sweep GC, and escape-only `call/cc`.
Its primitives are the desktop SDKs (graphics, audio, fs, screen), so
closures and higher-order functions drive real hardware. It coexists with
BASIC (`SCHEME` / `exit`), host-compiles for `make test` (with `call/cc`
and GC-stress tests), runs a seeded `DEMO.SCM` in smoke, and uses a
freestanding RV64 `setjmp` in the kernel. Optional Scheme follow-ups:
moving/compacting GC, full re-entrant `call/cc`, quasiquote, a windowed
Scheme editor.

## Delivered follow-ups (foundation is done)

- **BASIC string variables + EDIT (2026-07-17).** A$..Z$ with assignment,
  concatenation, `PRINT`, `INPUT`, and `IF A$ = "..."` / `<>` comparison.
  `EDIT n` replays a stored line into the editor for in-place editing.
- **Real-time clock (2026-07-17).** The goldfish RTC gives the machine
  wall-clock time; BASIC `TIME` (Unix seconds) and `TIME$` (`HH:MM:SS`).

---

# Backlog (2026-07-17 stopping point)

The charted course (M1–M7) is complete: a memory-protected, preemptively
multitasking, windowed, networked, self-hosting home computer in ~8,800
readable lines that boots to READY in under a second. What follows is
prioritized future work, none of it on a critical path. Every item must
preserve the invariant that one person can read the whole machine and that
`make check` gates every capability headlessly.

## 1. Networking — the open design decision (highest value, needs a choice)

The pivotal question is *where* reuse lives, not whether. The kernel stays
legible; heavy reused code belongs at the cartridge/library layer behind the
frozen ABI. Two legitimate directions (see the 2026-07-17 discussion):

- **1a. Bespoke minimal kernel TCP** (recommended). A single-connection,
  client-first TCP over the existing IP layer (~1–1.5K readable lines, no
  fragmentation), plus a small DNS resolver over UDP (QEMU user-net DNS is
  10.0.2.3). Writing the small legible thing *is* the experiment. Gate it
  deterministically with QEMU `guestfwd` + a scripted host TCP peer — this
  is what makes TCP shippable under the honesty rule, and it's a real
  milestone, not a bolt-on.
- **1b. A BASIC network surface + a `FETCH`/HTTP-get app** once TCP exists,
  closing "every capability reachable from the built-in language" for the
  network.
- **1c. (Alternative thesis) Port uIP or lwIP as a cartridge library** to
  demonstrate reuse at the app layer without touching the legible kernel —
  historical precedent: uIP/Contiki ran on a real C64 over an Ethernet
  cartridge.

Reference: uIP (~few thousand lines) fits the kernel philosophy; lwIP (tens
of thousands) belongs only as a cartridge library.

## 2. BASIC language depth

- String functions: `LEN`, `LEFT$`/`RIGHT$`/`MID$`, `CHR$`, `ASC`, `VAL`,
  `STR$`.
- Arrays (`DIM A(n)`), and user-defined `FN`.
- `DATA`/`READ`/`RESTORE`.

## 3. Window system + desktop

- Window **resize** and a close/minimize affordance (only move exists today).
- A graphical launcher/dock on the desktop layer (apps currently start via
  `RUN NAME`).
- Clipboard copy/paste between apps over IPC.
- Double-buffered present to remove any residual tearing.

## 4. Dev tools

- Richer assembler: macros, `.INCLUDE`, expression operands, a data section.
- A monitor/disassembler app (inspect memory and cartridges live).
- Stretch: a tiny-C subset compiler producing cartridges (large; the real
  self-hosting endgame).

## 5. Apps (each a separately loaded toolkit cartridge)

- Calculator, clock/watch (now that the RTC exists), hex editor, a small
  spreadsheet, one more game.
- Robot control panel driving the existing I2C/CAN SDK.

## 6. Infrastructure / honesty

- CI: run `make check` in GitHub Actions on push.
- Speed up the smoke gate (currently ~3 min; parallelize the two boots).
- Consider C26FS subdirectories, or deliberately keep the filesystem flat.

## Deliberately not shipped (yet)

TCP and DNS were declined rather than stubbed: they cannot be verified
deterministically without the `guestfwd` + host-peer harness described in
1a, and an ungated network claim would violate the machine-checked rule.
They are honest future work.

---

Principles that bound all of it: freestanding C and assembly only in the
target; a hobbyist can read the whole system; a capability is called
hardware-backed only when QEMU emulates the device; `make check` stays the
single gate; and the moment an addition would need Linux (or code no one can
read) is the moment it belongs at the cartridge layer, not in the kernel.
