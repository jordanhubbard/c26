# c26 Architecture

## Boot and execution model

QEMU loads `build/c26.elf` at `0x80000000`. `_start` establishes the stack and
enters `kmain`; the system then owns the hart without firmware or host OS
services. UART0 at `0x10000000` remains the diagnostic and serial-input path.

The kernel performs a deterministic startup demonstration and then remains in
an event loop polling UART, virtio input queues, and completed audio periods.
Polling is intentional for this single-hart version and does not require a PLIC
or scheduler.

## Device transport

`src/virtio.c` implements the modern virtio-MMIO state machine, feature
negotiation, split-queue setup, descriptor submission, used-ring completion,
memory barriers, and interrupt acknowledgement. QEMU exposes eight transports
between `0x10001000` and `0x10008000`; drivers probe them by device ID rather
than relying on command-line attachment order.

The current real emulated backends are:

| Device | Virtio ID | Driver | Behavior |
| --- | ---: | --- | --- |
| GPU | 16 | `src/framebuffer.c` | 2D resource, guest backing, scanout, transfer, flush |
| Keyboard/mouse | 18 | `src/input.c` | Pre-posted event buffers and live event recycling |
| Sound | 25 | `src/audio.c` | Stream query, parameter negotiation, PCM lifecycle and refill |

## Graphics stack

The framebuffer is always backed by a 640x480 32-bit software pixel array. If a
virtio GPU is present, that memory is attached to a scanout resource. Without a
GPU the same drawing APIs remain safe but have no physical scanout.

`src/graphics.c` provides the higher layers: clipped primitives, a bitmap font,
z-buffered triangle rasterization, interpolated vertex colors, a projected cube,
and an integer ray-sphere renderer. No host OpenGL calls or floating-point ABI
are needed.

## Input and desktop

Every virtio input device owns a queue of writable event buffers. Completed
Linux input events are translated into desktop navigation, relative pointer
motion, clicks, and BASIC characters. UART input feeds the same BASIC parser.
The desktop is redrawn and flushed after state changes.

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

`make smoke` performs a clean build, boots with modern virtio GPU, keyboard,
mouse, and sound devices, injects live BASIC commands over UART, and requires
markers from each backend and SDK path. Static boot text alone cannot satisfy
the gate because the test checks state-changing write/read results.
