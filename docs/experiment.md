# The c26 Experiment

## Hypothesis

A home computer with protected multitasking, a windowed desktop, real
networking, and modern media can fit in a codebase one person can read end
to end, boot to READY in about a second, and keep every capability
reachable from its built-in language. This is the property the industry
abandoned when the PC lineage won; c26 exists to check whether abandoning
it was necessary.

## Scorecard (final, 2026-07-17)

| Measure | Result |
| --- | --- |
| Kernel + SDK headers | ~7,000 lines of C and assembly |
| Applications (toolkit + 12 cartridges, incl. an assembler) | ~1,700 lines |
| Everything the guest runs | **~8,700 lines** — well under the 50k budget |
| Boot to READY | under a second of guest time (QEMU virt, TCG) |
| Syscall surface | 32 syscalls, one screen of c26_user.h |
| Self-hosting | on-board RV64 assembler builds runnable cartridges |
| Verification | make check: host unit tests + two-boot QEMU gate |

## What the machine does

One RV64 hart, M-mode kernel, and U-mode processes under Sv39: up to four
concurrent cartridges, each in its own address space with its own window,
scheduled round-robin, killed on fault or Ctrl-C without taking the machine
down. The BASIC console is the root layer of a compositor with movable,
z-ordered windows. Storage is a checksummed filesystem on virtio-block;
networking is a real IPv4/UDP stack on virtio-net, verified by datagrams
from the host; sound is an eight-voice 48 kHz mixer on virtio-sound;
graphics are a software-rendered framebuffer with a z-buffered rasterizer
and a CPU ray tracer. The built-in BASIC reaches all of it — drawing,
sound, devices, job control, files — and the application suite (file
manager, text editor, paint, music tracker, breakout, network mailbox)
is built out of tree against a frozen vector-table ABI.

## Honesty rules that held

- A capability is called hardware-backed only when QEMU emulates the
  device; the I2C/CAN fabrics are labelled local.
- Every claim has a machine-checked gate: persistence needs two OS
  processes, protection needs a contained wild-pointer write, concurrency
  needs interleaved output, windowing needs a framebuffer checksum change,
  networking needs a real datagram round trip.
- The gate cannot hear or see: real-hardware runs found a boot tone and a
  wall-of-text first screen that no marker string could. Human verification
  stays part of the method.

## Instructive failures

The bugs found en route were the classics, compressed into a week: a
partial register restore that only mattered once time slices made re-entry
real; an interrupt window in the user-mode entry path that recorded kernel
code as user state; a UART fast path racing its own interrupt handler; a
type-ahead drain that fed a queue back into itself. Each one now has a
regression gate.

## Self-hosting and the scriptable desktop (M7)

Two properties close the experiment. First, the machine develops software
for itself: `apps/asm` is a two-pass RV64 assembler that runs as a
protected cartridge, reads an assembly source file from C26FS, and emits a
runnable cartridge back to disk — `RUN ASM`, type `HELLO.ASM HI`, then
`RUN HI` executes machine-assembled code in its own address space, exactly
as the C64 shipped with the tools to program itself. Second, the desktop
is scriptable from the built-in language: BASIC gained `WINDOW j,x,y`,
`FOCUS j`, and `SEND j,"msg"`, so the window manager and inter-process
messaging are reachable from the same prompt that draws pixels and plays
notes. The smoke gate assembles HELLO.ASM on the machine, runs the result,
and delivers a BASIC `SEND` to a running job across address spaces.

## Verdict

The hypothesis held, with an order of magnitude to spare. About 8,700
lines of C and assembly — a codebase a single person can read across a
weekend — deliver a memory-protected, preemptively multitasking, windowed,
networked home computer that boots to READY in under a second, develops
software for itself, and exposes every capability it has to a BASIC prompt
a child could use. That combination was thought to require the complexity
the PC lineage accreted; it did not. The property the industry abandoned
when it chose that lineage — a whole machine one mind can hold — was not a
casualty of capability but of business model. c26 is an existence proof
that you can have both: the immediacy and legibility of 1982 with the
protection, concurrency, graphics, and networking of 2026, and nothing in
between that a curious person cannot open and understand.

What remains is not proof but breadth: string variables and a full-screen
program editor in BASIC, more of the IP stack (TCP, DNS), a richer
assembler (more pseudo-ops, macros), and more applications. The
foundation is done, and it is small enough to keep that way.
