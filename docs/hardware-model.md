# The c26 Hardware Model

The c26 answers “what would a Commodore-style home computer look like in 2026?”
with an instant-on, inspectable machine rather than a miniature general-purpose
PC. It keeps direct programmability and a friendly built-in language while
replacing magic memory addresses with typed SDK calls.

## Design principles

- **Instant ownership:** the user's program boots without a host OS beneath it.
- **One coherent machine:** graphics, sound, input, devices, and robots share a
  small C API rather than separate vendor frameworks.
- **Capability APIs:** device registers, messages, and queues replace unexplained
  PEEK/POKE addresses. Compatibility commands remain educational aliases.
- **Modern media, understandable implementation:** a 32-bit framebuffer,
  z-buffered 3D, ray tracing, stereo PCM, keyboard, and mouse are implemented in
  code that a hobbyist can read end to end.
- **Backend honesty:** a feature is labelled as hardware-backed only when QEMU
  exposes an emulated device. Other buses are explicitly local deterministic
  fabrics.

## Current machine profile

| Capability | c26 contract |
| --- | --- |
| CPU | One RV64IMAC hart, freestanding execution |
| Interrupts | CLINT timer and PLIC-routed UART/virtio IRQs; WFI idle |
| Storage | 8 MiB virtio-block disk; C26FS v2: 64 files, 128 KiB, delete/rename |
| Cartridges | Flat RV64 binaries from C26FS via the c26_api.h vector table |
| Protection | U-mode + Sv39 per cartridge; 25-syscall surface; preemptive kill |
| Multitasking | 4 concurrent processes, round-robin slices, JOBS/KILL |
| Windows | Movable z-ordered windows over the console root; click/drag/Tab focus |
| IPC | Bounded per-process mailboxes; send/recv syscalls |
| Applications | c26_ui toolkit; FILES browser/launcher (spawn), EDIT text editor |
| Networking | virtio-net + ARP/ICMP/UDP; kernel echo :2600; app udp syscalls |
| Power | BYE/HALT power off via the SiFive test finisher |
| Self-hosting | On-board two-pass RV64 assembler builds runnable cartridges |
| Scripting | BASIC WINDOW/FOCUS/SEND drive the window manager and IPC |
| Clock | Goldfish RTC wall-clock time; BASIC TIME and TIME$ |
| Display | 1920x1080x32 software surface on a virtio-GPU scanout |
| Console | 100x45 scrolling text console, printable-ASCII 5x7 font |
| 2D/3D | Pixel primitives, text, depth-tested colored triangles |
| Ray tracing | Integer CPU ray/sphere renderer |
| Audio | Eight voices mixed to 48kHz S16 stereo and virtio-sound |
| Input | Virtio keyboard and relative mouse plus serial console |
| User language | C26 BASIC V3: expressions, control flow, INPUT/GET, hardware statements, save/load |
| Second language | c26 Scheme: integer Lisp REPL (GC, tail calls, call/cc); primitives are the desktop SDKs |
| Device SDK | Register, I2C, CAN and port-addressed packet calls |
| Robotics | Sensor and motor SDK layered over I2C and CAN |

This profile is intentionally small enough to understand, but each public API
has real state transitions and observable behavior. That is the core difference
between c26 and the original print-only demonstration.
