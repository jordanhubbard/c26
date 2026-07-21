# c26 Architecture

## Boot and execution model

QEMU loads `build/c26.elf` at `0x80000000`. `_start` establishes the stack and
enters `kmain`; the system then owns the hart without firmware or host OS
services. UART0 at `0x10000000` remains the diagnostic and serial-input path.

`src/trap.S` preserves the interrupted register set and enters the M-mode trap
handler in `src/interrupts.c`. A periodic CLINT interrupt supplies ticks. The
PLIC routes UART IRQ 10 and virtio IRQs 1-8; UART receive data is buffered by
its handler and virtio sources are acknowledged before their used rings are
drained. After servicing application work, the single hart sleeps with `WFI`
until the next timer or device interrupt.

## Device transport

`src/virtio.c` implements the modern virtio-MMIO state machine, feature
negotiation, split-queue setup, descriptor submission, used-ring completion,
memory barriers, and interrupt acknowledgement. QEMU exposes eight transports
between `0x10001000` and `0x10008000`; drivers probe them by device ID rather
than relying on command-line attachment order.

The current real emulated backends are:

| Device | Virtio ID | Driver | Behavior |
| --- | ---: | --- | --- |
| Block | 2 | `src/block.c` | 512-byte read/write requests and negotiated flush |
| GPU | 16 | `src/framebuffer.c` | 2D resource, guest backing, scanout, transfer, flush |
| Keyboard/mouse | 18 | `src/input.c` | Pre-posted event buffers and live event recycling |
| Sound | 25 | `src/audio.c` | Stream query, parameter negotiation, PCM lifecycle and refill |

## Graphics stack

The framebuffer is always backed by a 2560x1440 32-bit software pixel array. If a
virtio GPU is present, that memory is attached to a scanout resource. Without a
GPU the same drawing APIs remain safe but have no physical scanout.

`src/graphics.c` provides the higher layers: clipped primitives, a bitmap font,
z-buffered triangle rasterization, interpolated vertex colors, a projected cube,
and an integer ray-sphere renderer. No host OpenGL calls or floating-point ABI
are needed.

## Console, input, and desktop

`src/console.c` owns a 100x45 cell text console rendered onto the software
framebuffer with a full printable-ASCII 5x7 font (`src/framebuffer.c`). All
terminal output (`c26_puts` and friends) writes both the UART and the console
cells, so the serial stream and the display always agree; presentation is
throttled to one full-screen GPU transfer per few timer ticks. The machine has
three screen modes: CONSOLE (default at boot), DESKTOP (launcher), and GFX
(BASIC graphics screen).

Every virtio input device owns a queue of writable event buffers. Completed
Linux input events are translated per mode: console keys feed BASIC, Esc
toggles the desktop, desktop arrows/mouse/Enter select and launch
applications. UART input feeds the same BASIC parser through an
interrupt-driven ring with real backpressure — when buffers fill, the RX
interrupt is masked and QEMU withholds delivery rather than bytes being
dropped. The Files application reads C26FS directory metadata and shows saved
names and sizes.

## BASIC

`src/basic.c` implements C26 BASIC V3: a recursive-descent expression
evaluator over signed 64-bit integers (`+ - * / MOD`, parentheses,
comparisons, `AND`/`OR`/`NOT`, functions `RND`, `ABS`, `PEEK`, `TI`, `FB`),
a program-counter run engine (`IF...THEN`, `GOTO`, `GOSUB`/`RETURN`,
`FOR`/`NEXT`/`STEP`, `END`), blocking interaction (`INPUT`, `GET`, `PAUSE`)
that keeps pumping I/O and audio while it waits, and hardware statements
(`SCREEN`, `CLS`, `COLOR`, `PLOT`, `LINE`, `RECT`, `TEXT`, `SOUND`,
`DEVICE`, `ROBOT`) that wrap the C SDKs. During RUN, typed characters queue
for `GET`/`INPUT` and Ctrl-C or Esc raises a break; leftover type-ahead
returns to the line editor when the program ends. The `FB` function returns a
checksum of the framebuffer so graphics output is verifiable headlessly. A
fresh filesystem gets a `DEMO` program — the boot demo is written in the
machine's own language.

## Persistent storage

`src/block.c` drives a modern virtio-block queue synchronously during bounded
filesystem operations. `src/fs.c` owns the C26FS v2 format: a checksummed
superblock (sector 0), a directory of 64 fixed 32-byte entries (sectors 1-4),
a free-sector bitmap (sectors 5-8), and contiguous first-fit data extents.
Files hold up to 128 KiB, each with a content checksum; overwrites reuse
their extent when the new size fits and relocate when it grows, and DELETE
and RENAME are supported. `scripts/fsinstall.py` implements the identical
layout host-side so cartridges can be installed into disk images; the smoke
gate has each implementation read what the other wrote.

## Cartridges

`include/c26_api.h` is the frozen ABI: a cartridge is a flat RV64 binary
linked at `0x88000000`, starting with a validated header, entered as
`int app_main(const c26_api_t *api)`. The API vector table exposes console,
input (keys, mouse, break), time, framebuffer primitives, audio voices,
C26FS, and device registers. `RUN "NAME"` in BASIC launches it.

## Protection

The kernel stays in M-mode; cartridges execute in U-mode under Sv39. Before
entry, `src/cart.c` builds an address space (`src/vm.c`) that identity-maps
only what belongs to the app: its 2 MiB load region, a private 64 KiB stack,
the framebuffer surface, one scratch page, and a single read/execute page of
syscall stubs. Kernel memory and MMIO are simply absent. The `c26_api_t`
pointers land in the stub page, which raises `ecall`s — the syscall numbers
in `include/c26_user.h` are the entire kernel surface, and every pointer
argument is validated against the mapped regions.

`src/trap.S` discriminates trap origin via `mscratch`: kernel traps use the
old path, user traps save the full register file into a frame, switch to the
kernel stack, and dispatch (`c26_trap_handler_user` in `src/cart.c`). On
every timer or device interrupt the kernel pumps I/O and audio, so a
spinning app cannot starve the machine, and Ctrl-C terminates it
preemptively. A fault (wild pointer, illegal instruction) prints the cause
and kills the process; the console returns and the machine keeps working.
`apps/crash` and `apps/spin` exist to prove both properties in the smoke
gate.

## Processes and the compositor

Up to four cartridges run concurrently. Every cartridge links at
`C26_CART_BASE`; each process's page tables map that VA to a different
physical slot, so the classic single-load-address limitation disappears
under Sv39. The kernel main loop hands out round-robin time slices
(`c26_cart_schedule`); the timer trap ends a slice by unwinding to the
scheduler, and `yield` gives one up voluntarily. The kernel's context lives
in globals rather than any process frame, so any process's fault, exit, or
kill unwinds cleanly no matter which one was running.

Each process owns a private surface and a movable window: the BASIC console
renders as the root layer and windows composite over it in z-order with
borders and title bars. Drawing syscalls render into the surface, `present`
marks damage, and apps lay themselves out to `window_size()` (ABI v2) with
window-local mouse coordinates. Clicking focuses and raises a window;
dragging the title bar moves it; clicking the console focuses it; Tab
(keyboard) or Ctrl-T (serial) cycle focus — all while every job keeps
running. Ctrl-C kills the focused job; `JOBS` lists them, `KILL n` stops
one. `apps/ticker` makes the concurrency visible in the smoke gate, and the
gate also checks that a window changes the composited framebuffer checksum.

Jobs communicate through bounded mailboxes: `send(job, bytes)` and
`recv(&from, buffer)` syscalls move up to 240-byte messages, four queued per
process, copied by the kernel between address spaces. `apps/ping` and
`apps/pong` prove the round trip in the smoke gate.

BASIC stores up to 256 sorted numbered lines of 80 characters. `SAVE`
serializes those lines into a C26FS file, while `LOAD` validates and rebuilds
the in-memory program.

## Audio

The software mixer owns eight voices with 32-bit phase accumulators, square,
saw, triangle and noise waveforms, independent volume/pan, and attack envelopes.
It emits interleaved signed 16-bit stereo at 48 kHz.

The virtio-sound driver discovers a compatible output stream, selects stereo
S16/48k parameters, prepares and starts it, pre-buffers four periods, and
re-renders each period after device completion. A missing sound device only
removes physical output; it does not disable the mixer SDK.

## Device and robot SDKs

The non-virtio SDK fabric is deliberately stateful but local:

- Register reads and writes replace raw address poking as the primary API.
- I2C provides addressed register transactions.
- CAN provides bounded frame queues.
- Networking provides bounded packet loopback with port metadata.
- The robot SDK samples I2C registers and publishes motor commands over CAN.

These backends make API behavior testable without implying a physical bus or
external network that QEMU was not configured to provide.

## Validation contract

`make smoke` builds the image, creates a fresh raw disk, and boots modern virtio
block, GPU, keyboard, mouse, and sound devices. The first QEMU process verifies
CLINT/PLIC counters, expression evaluation, FOR/GOSUB/IF control flow, an
INPUT round-trip, framebuffer checksums that change after drawing statements,
SOUND argument validation, live device writes, and new-file save and
overwrite. It is terminated completely. A second QEMU process mounts the same
disk, loads and runs the stored BASIC text and the installed DEMO program.
Static boot text or a single-process RAM cache cannot satisfy the gate.
