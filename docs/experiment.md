# The c26 Experiment

## Hypothesis

A home computer with protected multitasking, a windowed desktop, real
networking, and modern media can fit in a codebase one person can read end
to end, boot to READY in about a second, and keep every capability
reachable from its built-in language. This is the property the industry
abandoned when the PC lineage won; c26 exists to check whether abandoning
it was necessary.

## Scorecard (2026-07-17)

| Measure | Result |
| --- | --- |
| Kernel + SDK headers | ~6,700 lines of C and assembly |
| Applications (toolkit + 11 cartridges) | ~1,100 lines |
| Everything the guest runs | **~7,800 lines** — well under the 50k budget |
| Boot to READY | under a second of guest time (QEMU virt, TCG) |
| Syscall surface | 32 syscalls, one screen of c26_user.h |
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

## Verdict so far

The hypothesis is holding with an order of magnitude to spare: ~7,800
lines buy what 1982 could not imagine and 2026 buries under hundreds of
millions. Remaining before the experiment closes: self-hosted development
(an on-machine assembler producing runnable cartridges) and scripting the
desktop from BASIC — the final test of "every capability reachable from
the built-in language."
