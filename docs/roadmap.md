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

## Delivered: TCP + DNS — the network is reachable end to end (2026-07-19)

The 2026-07-17 backlog declined TCP and DNS because they could not be gated
deterministically. Both now ship, gated:

- **A single-connection kernel TCP client** in `src/net.c`: a compact state
  machine (SYN handshake with retransmit, in-order receive that only ACKs
  what it buffers, FIN/RST teardown) over the existing IPv4 layer, with a
  pseudo-header checksum. `c26_tcp_connect/send/recv/close` and BASIC `TCP
  a,b,c,d,port` / `TCP SEND` / `TCP RECV` / `TCP CLOSE`.
- **The `guestfwd` + scripted host peer harness** that makes TCP shippable
  under the honesty rule: QEMU's own TCP terminates the guest side and pipes
  the stream to `scripts/tcp_peer.py`, so the smoke gate drives a full
  handshake / send / recv / close against a real, deterministic peer with no
  external network.
- **A DNS A-record resolver over UDP** to QEMU user-net's 10.0.2.3, with a
  dotted-quad fast path; BASIC `RESOLVE "host"`. The pure query/parse codec
  lives in `include/c26_dns.h` and is host-tested (`tests/test_dns.c`,
  including name compression and CNAME-chain skipping); the recursive path is
  verified live (`RESOLVE "example.com"`), and the smoke gate asserts the
  deterministic literal path.

Immediate-mode network statements pump only the network while they wait, not
console input, so a blocking `TCP`/`RESOLVE` cannot re-enter the line editor.
Optional follow-ups: a BASIC `TCP "host",port` connect-by-name, and a
`FETCH`/HTTP-get cartridge closing the built-in-language network surface.

## Delivered: window management — resize, minimize, close (2026-07-19)

Windows were movable only. The compositor (`src/cart.c`) now draws titlebar
affordances — a minimize/restore box and a close box — plus a bottom-right
resize grip, and the hit tester shares one set of geometry helpers with the
drawing code so a click always lands on the affordance that was drawn.
Minimized windows collapse to their title bar; resizing clamps `win_w/win_h`
(the content is a window onto the app's full-screen surface, so no
reallocation) and apps relayout on their next `window_size()` poll. BASIC
gained `WINDOW SIZE j,w,h`, `WINDOW MIN/MAX j`, and `WINDOW CLOSE j` alongside
the existing `WINDOW j,x,y` move, so every operation is scriptable — the smoke
gate resizes, minimizes, restores, and closes a running cart's window and
asserts that each step changes the composited framebuffer (FB checksums) and
that the close terminates the job.

Note on double buffering (backlog item): the machine renders into the single
virtio-GPU scanout backing and only then issues `TRANSFER_TO_HOST_2D` +
`RESOURCE_FLUSH`, which snapshots the frame atomically — the host never sees a
half-drawn buffer. A second back buffer would add copies without removing any
observable tearing, so it is deliberately not shipped rather than added as an
unmotivated claim.

## Delivered: a shared clipboard — copy/paste across apps (2026-07-19)

One kernel-held text buffer (`src/cart.c`) is the system clipboard, reachable
three ways: the ABI grew `clip_set`/`clip_get` (v3) so U-mode cartridges copy
and paste through the syscall seam; BASIC gained `CLIP "text"` and `PASTE`;
and EDIT binds Ctrl-W (copy the current line) and Ctrl-Y (paste at the
cursor). Because every path reads and writes the same buffer, copy/paste
crosses the app boundary. The smoke gate proves both directions headlessly: a
BASIC round-trip, then EDIT pasting what BASIC copied (`clip_get` reads the
5-byte "PIECE"), then EDIT copying a line that BASIC's `PASTE` reads back
(`clip_set`). The stub page stayed within its single 4 KiB page.

## Delivered: a graphical dock + synthetic pointer input (2026-07-19)

Apps used to start only by name (`RUN NAME`). A persistent dock now runs along
the screen bottom: the compositor (`src/cart.c`) rebuilds it from C26FS at
boot, making a tile for every file that is a real cartridge (magic-header
probe via the new `c26_fs_peek`), and a pointer click on a tile launches that
app through the ordinary window-manager hit test.

To gate a mouse affordance under the headless rule, the desktop gained
synthetic pointer input (`c26_desktop_inject_pointer` / `_button`), surfaced as
BASIC `CLICK x,y` and `DRAG x1,y1,x2,y2`, which flow through the exact path a
real virtio-mouse event takes. This also lets the smoke gate exercise the
window affordances by *clicking* them, not just scripting them: `BASIC DOCK`
prints each tile's centre so a click can land on it precisely, then the gate
clicks PAINT's tile and confirms it launched (a second `CART START PAINT`
beyond the one from `RUN`). With this, every windowing capability — move,
resize, minimize, close, and launch — is driven through the real pointer code.

## Delivered: BASIC language depth (2026-07-19)

Three rounds of interpreter growth in `src/basic.c`, each covered by host
assertions in `tests/test_basic.c` (the real interpreter driven through
`feed_char`) and demonstrated end to end in the smoke gate:

- **String functions** — `LEN`, `LEFT$`, `RIGHT$`, `MID$` (optional length),
  `CHR$`, `ASC`, `VAL`, `STR$`. The string engine became a term-based
  evaluator (`eval_string_term` + `+` concatenation); `LEN`/`ASC`/`VAL` join
  the numeric factor parser.
- **Arrays and user functions** — `DIM A(n)` (comma lists, 0..n, auto-dim to
  0..10, bounds-checked) from a shared bump pool, and single-parameter
  `DEF FN A(x)=expr` stored as source and re-parsed per call. Both reset on
  `NEW` and at the start of every `RUN`; scalars persist as before. Function
  and array names are single letters, matching the machine's variable model.
- **DATA / READ / RESTORE** — a cursor walks `DATA` statements in program
  order; `READ` pulls the next items into scalar, string, or array variables
  (`OUT OF DATA` at the end); `RESTORE` rewinds to the start or to a line.

## Delivered: faster smoke gate, and C26FS stays flat (2026-07-19)

`scripts/smoke.py`'s staged second boot now paces on the guest's own output:
each stage may carry a completion marker and proceeds as soon as it prints
(capped by the old delay as a timeout) instead of always sleeping the full
interval. Silent or FB-flush-timing-sensitive stages keep a fixed delay. A
pure idle-detection version was tried and rejected — it raced ahead of
silent-then-print work (graphics, the self demo) — so the marker approach is
the one that stays reliable. Result: the smoke runs in ~106s (was ~3 min),
green across repeated runs.

C26FS subdirectories were considered and **deliberately declined**: a flat
64-file store with first-fit extents is something one person can read in a
sitting (`src/fs.c`), and paths/directory nodes/traversal would add real
complexity for little benefit at this scale. Keeping the filesystem flat is
the honest choice, consistent with the legibility principle; it can be
revisited if the file count ever outgrows a flat namespace.

## Delivered: continuous integration (2026-07-19)

`.github/workflows/ci.yml` runs the single gate — `make check` (host unit
tests + the two-boot headless QEMU smoke) — on every push and pull request.
The runner installs the freestanding toolchain from stock packages (PATH
`clang` + `lld` + `llvm-objcopy`, which the Makefile already falls back to
when Homebrew LLVM is absent) and `qemu-system-misc`, so no bespoke setup is
needed. The honesty rule is now enforced automatically, not by hand; a green
badge on the README reflects the live gate (first run: 4m04s end to end).

## Consolidation review (2026-07-19)

A pass of adversarial reviews over the session's new code (apps, the RV64
disassembler and tiny-C codegen, the BASIC additions, the TCP/DNS stack, and
the window-manager/clipboard/dock/pointer code) found and fixed a set of real
bugs that the happy-path smoke could not reach:

- **BASIC:** a self-recursive `DEF FN` now raises `FORMULA TOO COMPLEX`
  instead of overflowing the native stack; a `DIM` with a huge subscript is
  rejected (was an integer-overflow past the pool bounds); `MID$` with a
  negative length is empty; a non-numeric `DATA` item advances the cursor
  (was an infinite `READ` loop). Regression tests added.
- **TCP:** `handle_tcp` now bounds the payload by the real frame size (a lying
  IP total-length could drive an out-of-bounds read), validates the data
  offset, fixes FIN detection across a 32-bit sequence wrap, and advertises
  the actual free receive window.
- **Apps:** the calculator keeps its result when chaining after `=`; snake no
  longer instantly self-collides on some resets.
- **Tools:** the disassembler decodes the M-extension (`mul`/`div`/`rem`,
  which tiny-C emits — they were mislabeled as `add`/`xor`/`or`); tiny-C now
  synthesizes full 64-bit integer literals instead of silently truncating to
  32 bits.
- **WM:** a title-bar drag or corner resize is cancelled when input leaves for
  the desktop (Esc), so a window no longer sticks to the pointer.

The reviewers verified the hypothesized worst case — a U-mode cartridge
escaping its sandbox through the clipboard syscalls — is **not** possible: the
`clip_set`/`clip_get` paths validate the full pointer range through
`c26_vm_translate` before any copy.

Known, deliberately-scoped limitations of the minimal network stack (documented
rather than papered over): the single-connection TCP client does not
retransmit *data* or a lost FIN (only the SYN), and the DNS resolver trusts a
response on its transaction ID without matching the question name — both
acceptable on the lossless, non-adversarial QEMU user-net path, and honest
future work if the stack ever faces a real network.

## Delivered: a self-hosting tiny-C compiler (2026-07-19)

`apps/tinyc` compiles a small C subset from a C26FS source file straight to a
runnable RV64 cartridge, on the machine — the self-hosting endgame the backlog
flagged as the hardest stretch. The subset: an implicit `main` body with `int`
declarations, assignment, `print(expr)`, `if/else`, `while`, `return`, and
expressions over `+ - * /` and the six comparisons with C precedence. Codegen
is a single-pass stack machine with branch backpatching, reusing the RV64
encoders and cartridge-header format from `apps/asm`; it emits M-extension
`mul`/`div` and keeps the api pointer in `s0` with a 32-slot variable frame.
The smoke gate compiles `tests/tinyc/sum.c` — a `while` loop summing 1..10 —
into a cartridge and runs it, requiring the output `55` (the loop) and `10`
(`2*3+4`, exercising precedence). With this the machine has three ways to
program itself: BASIC, Scheme, and now C through the on-board compiler, plus
assembly through the assembler.

## Delivered: richer on-board assembler (2026-07-19)

`apps/asm` gained a preprocessor pass (run before its two assembly passes, so
the existing self-hosting path is untouched): `.MACRO`/`.ENDM` with `\1..\9`
argument substitution, macro invocation, and `.INCLUDE` of another C26FS
source. Operands may now be expressions — numbers and symbols joined by
`+ - *`, applied left to right (`MSG+4`, `END-START`, `617+617`) — on top of
the existing data section (`.BYTE`/`.WORD`/`.QUAD`/`.ASCIZ`/`.ALIGN`). The
smoke gate assembles `tests/asm/feat.asm` (which exercises a macro, an
include, and an expression) and runs the result, which prints the included
message and the expression's value (1234). `scripts/fsinstall.py` now accepts
`.` in names, matching C26FS, so host-side source fixtures install cleanly.

## Delivered: application suite + an RV64 disassembler (2026-07-19)

Seven new cartridges, each a U-mode program built on the frozen ABI and gated
in the smoke run (launch → drive → read a marker → quit):

- **CALC** — a four-function calculator (mouse keypad + keyboard), prints
  `CALC = <n>` per result.
- **CLOCK** — a digital watch reading the goldfish RTC through a new ABI v4
  `rtc_seconds()` syscall; shows `HH:MM:SS`.
- **SHEET** — a 5×6 integer spreadsheet with live row/column sums and a grand
  total (`SHEET TOTAL <n>`).
- **ROBOT** — a control panel that writes channel values to the device fabric
  (`dev_write8`) and reads them back (`dev_read8`), proving the control loop.
- **HEXEDIT** — a hex editor over C26FS files (nibble editing, Ctrl-S save).
- **SNAKE** — a self-playing-capable game (LCG food, tick-paced movement).
- **MONITOR** — a real RV64 disassembler: it decodes the instruction subset
  the on-board assembler emits (LUI/AUIPC/JAL/JALR, OP-IMM, OP, loads,
  stores, branches, ECALL), shows a hex + mnemonic view of any C26FS file,
  and defaults to the machine-assembled `HI` cartridge — RUN ASM to build a
  program, RUN MONITOR to read it back. A startup self-test disassembles
  known words (`addi x10, x0, 10`, `add x10, x10, x11`, `ret`) so the decoder
  is machine-checked deterministically.

## Delivered follow-ups (foundation is done)

- **BASIC string variables + EDIT (2026-07-17).** A$..Z$ with assignment,
  concatenation, `PRINT`, `INPUT`, and `IF A$ = "..."` / `<>` comparison.
  `EDIT n` replays a stored line into the editor for in-place editing.
- **Real-time clock (2026-07-17).** The goldfish RTC gives the machine
  wall-clock time; BASIC `TIME` (Unix seconds) and `TIME$` (`HH:MM:SS`).

---

# Backlog (2026-07-17 stopping point)

**Status (2026-07-19): worked through.** Everything below has since shipped and
is gated by `make check` — a second built-in language (Scheme), real TCP + DNS,
window management + dock + clipboard, BASIC depth (strings, arrays, DEF FN,
DATA/READ), CI, a faster smoke gate, a six-app suite, an RV64 disassembler, a
richer assembler, and a self-hosting tiny-C compiler — as recorded in the
"Delivered" sections above. The only items deliberately declined are the
double-buffered present and C26FS subdirectories (both unnecessary, with
rationale above), and the optional `FETCH` app / uIP port under networking.

The charted course (M1–M7) is complete: a memory-protected, preemptively
multitasking, windowed, networked, self-hosting home computer that boots to
READY in under a second. What follows is the original prioritized backlog,
kept for the record. Every item preserved the invariant that one person can
read the whole machine and that `make check` gates every capability headlessly.

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
