# c26

c26 is an instant-on RISC-V home computer for 2026. It boots directly on
QEMU `virt` into a full-screen BASIC console on its own display, runs a real
line-numbered BASIC with expressions, control flow, and hardware statements,
persists programs to disk, and exposes C SDKs for graphics, audio, storage,
devices, and robots. The target image is freestanding C and assembly with no
host operating system.

## Implemented system

- M-mode trap handling, a 10 MHz CLINT timer, PLIC-routed UART/virtio
  interrupts, and an idle loop that sleeps with `WFI`.
- Modern virtio-MMIO transport shared by block, GPU, input, and sound drivers.
- A persistent 8 MiB raw virtio-block disk with C26FS v2: 64 checksummed
  files up to 128 KiB, a free-sector allocation bitmap, and DELETE/RENAME.
  New disks format automatically, existing disks mount at boot, and a fresh
  machine installs a `DEMO` program written in BASIC.
- A cartridge port: apps are flat RV64 binaries in C26FS, compiled out of
  tree against the frozen `include/c26_api.h` vector table and launched with
  `RUN "NAME"`. `apps/paint` ships as the first cartridge (mouse draws, 1-8
  pick colors); `scripts/fsinstall.py` installs cartridges from the host.
- Memory protection: cartridges run in U-mode inside their own Sv39 address
  space, reaching the machine only through a 25-entry syscall table backed
  by a user-mapped stub page — the `c26_api_t` contract is unchanged. A wild
  pointer faults and kills the app, not the machine; the timer preempts, so
  input and audio stay alive under a spinning app and Ctrl-C kills it
  (`apps/crash` and `apps/spin` prove both in the smoke gate).
- Multiprocessing: up to four cartridges run concurrently in round-robin
  time slices. All cartridges link at the same address — per-process page
  tables map it to different physical slots. Tab / Ctrl-T switch focus
  without stopping anything; `JOBS` lists and `KILL n` terminates;
  `apps/ticker` prints a heartbeat that interleaves with console work as
  the smoke gate's concurrency proof.
- A windowed desktop: the BASIC console is the root layer and every process
  owns a movable, decorated window composited over it in z-order — click to
  focus, drag the title bar to move. Apps query their window size (ABI v2)
  and get window-local mouse coordinates.
- IPC: bounded message passing between jobs (`send`/`recv` syscalls with
  per-process mailboxes); `apps/ping` and `apps/pong` prove a round trip
  across address spaces in the smoke gate.
- A UI toolkit (`apps/lib/ui.*`) and a real application suite built on it:
  `FILES` (browse, launch via the `spawn` syscall, delete, or open a file
  in the editor — the filename travels over IPC), `EDIT` (a windowed text
  editor), `TRACKER` (an 8-voice step sequencer with saved patterns),
  `BREAKOUT` (mouse paddle, mixer sound effects), and `NET` (a UDP mailbox
  that ACKs every datagram reaching it from the host).
- Self-hosting: `apps/asm` is a two-pass RV64 assembler that runs on the
  machine, turning an assembly source file from C26FS into a runnable
  cartridge (`RUN ASM`, type `HELLO.ASM HI`, then `RUN HI`). A fresh disk
  seeds `HELLO.ASM` so the machine can program itself out of the box.
- A scriptable desktop: BASIC statements `WINDOW j,x,y`, `FOCUS j`, and
  `SEND j,"msg"` drive the window manager and inter-process messaging from
  the built-in language.
- Power control from inside the machine: `BYE`/`EXIT`/`QUIT`/`SHUTDOWN`
  say goodbye and power off through QEMU virt's SiFive test finisher;
  `HALT` is the blunt debug variant — immediate exit, no ceremony.
- 640x480 32-bit virtio-GPU scanout with software-buffer fallback, rendering
  a scrolling 100x45 text console with a full printable-ASCII 5x7 font.
- The machine boots to the BASIC console; Esc opens a desktop launcher with
  keyboard selection, mouse pointer, and BASIC, file-browser, robot, network,
  and device applications.
- C26 BASIC V3: signed 64-bit expressions with precedence, parentheses,
  comparisons, `AND`/`OR`/`NOT`/`MOD`; `IF...THEN`, `GOTO`, `GOSUB`/`RETURN`,
  `FOR`/`NEXT`/`STEP`, `END`, `REM`; `INPUT`, `GET`, `PAUSE`; functions
  `RND`, `ABS`, `PEEK`, `TI`, `FB`. Ctrl-C (serial) or Esc (keyboard) breaks
  a running program.
- BASIC hardware statements over the SDKs: `SCREEN`, `CLS`, `COLOR` (C64
  palette or 24-bit RGB), `PLOT`, `LINE`, `RECT`, `TEXT`, `SOUND`, `DEVICE
  READ/WRITE`, `ROBOT`, plus `PEEK`/`POKE` compatibility aliases.
- `LIST`, `EDIT`, `RUN`, `NEW`, `DIR`, `SAVE`, `LOAD`, `DELETE`, `RENAME`
  manage stored programs (256 lines of 80 characters); `RUN NAME` launches
  a cartridge; `EDIT n` recalls a line for in-place editing.
- String variables `A$`..`Z$` with assignment, concatenation, `PRINT`,
  `INPUT`, and `IF A$ = "..."` comparison, alongside the numeric engine.
- `c26_gl`-style software 3D SDK with filled triangles, color interpolation,
  a depth buffer, and a CPU ray tracer.
- Eight-voice, 48 kHz stereo audio mixer with waveforms, pan, envelopes, and a
  continuously refilled virtio-sound PCM stream.
- Stateful I2C register, CAN frame, and packet-loopback APIs, plus a robot SDK
  that uses I2C sensors and CAN motor commands.

The public SDK surfaces are in `include/c26_graphics.h`,
`include/c26_audio.h`, `include/c26_input.h`, `include/c26_block.h`,
`include/c26_fs.h`, `include/c26_console.h`, and `include/c26_devices.h`.
Cartridges see the machine through `include/c26_api.h` only.

## Build and run

Requirements:

- LLVM/Clang with `riscv64-unknown-elf` support.
- QEMU with the RISC-V system target and virtio block, GPU, input, and sound
  devices.
- Python 3 and Make.

```bash
make smoke          # Full headless hardware, language, and persistence gate
make run            # Graphical QEMU desktop with host audio
make run-headless   # UART console and dummy audio backend
```

`make run` and `make run-headless` preserve the guest disk at
`build/c26.img`. Delete that image to start with a blank computer. Inside
BASIC:

```text
] LOAD DEMO
] RUN
] NEW
] 10 FOR I=1 TO 5
] 20 PRINT I*I
] 30 NEXT
] RUN
] SAVE SQUARES
```

QEMU's RISC-V virt machine defaults its MMIO transports to the legacy
interface. The supplied run targets select modern virtio-MMIO explicitly;
custom QEMU invocations must also pass:

```text
-global virtio-mmio.force-legacy=false
```

## Controls

- The machine boots into the BASIC console; type on the virtio keyboard or
  the serial terminal.
- Esc opens the desktop launcher (arrows or mouse select, Enter launches,
  Esc returns to BASIC).
- Ctrl-C (serial) or Esc (keyboard) interrupts a running BASIC program.
- `SCREEN 1` switches to the graphics screen for `PLOT`/`LINE`/`RECT`/`TEXT`;
  `SCREEN 0` returns to the console.
- The Files application lists persisted program names and sizes from C26FS.

## Backend boundaries

Block storage, GPU, keyboard, mouse, PCM audio, and **networking** have real
QEMU emulated-device backends. The network stack is a small honest IPv4
implementation over virtio-net — ARP, ICMP echo reply, and UDP — on QEMU's
user network (guest 10.0.2.15, gateway 10.0.2.2). A kernel UDP echo service
on port 2600 is reachable from the host through `hostfwd`, and cartridges
get `udp_bind`/`udp_send`/`udp_recv` syscalls. The I2C and CAN SDKs remain
deterministic in-kernel fabrics for safe demos; they do not claim physical
buses. The 3D and ray-tracing paths run on the RISC-V CPU, which keeps their
behavior available without a host graphics API.

## Course

`docs/roadmap.md` records the forward course: honest virtio-net networking,
C26FS growth, user C program loading, host-side unit tests, and BASIC string
variables with a full-screen editor.
