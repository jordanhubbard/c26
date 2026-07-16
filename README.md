# c26

c26 is an instant-on RISC-V home computer for 2026. It boots directly on
QEMU `virt`, presents a graphical desktop, accepts keyboard and mouse input,
runs an interactive persistent BASIC environment, and exposes C SDKs for
graphics, audio, storage, devices, networking, and robots. The target image is
freestanding C and assembly with no host operating system.

## Implemented system

- M-mode trap handling, a 10 MHz CLINT timer, PLIC-routed UART/virtio
  interrupts, and an idle loop that sleeps with `WFI`.
- Modern virtio-MMIO transport shared by block, GPU, input, and sound drivers.
- A persistent 2 MiB raw virtio-block disk containing bounded, checksummed
  C26FS files. New disks format automatically and existing disks mount at boot.
- 640x480 32-bit virtio-GPU scanout with software-buffer fallback.
- Interactive graphical desktop with keyboard selection, mouse pointer, and
  launchable BASIC, file-browser, robot, network, and device applications.
- `c26_gl`-style software 3D SDK with filled triangles, color interpolation,
  and a depth buffer.
- CPU ray tracer used for the two-sphere desktop demonstration.
- Eight-voice, 48 kHz stereo audio mixer with waveforms, pan, envelopes, and a
  continuously refilled virtio-sound PCM stream.
- Interactive BASIC input over UART or virtio-keyboard. Numbered lines form a
  stored program; `LIST`, `RUN`, `NEW`, `DIR`, `SAVE`, and `LOAD` manage it.
  Immediate `PRINT`, `LET`, device commands, `PEEK`, and `POKE` remain
  available. PEEK/POKE are compatibility aliases implemented through the
  device API.
- Stateful I2C register, CAN frame, and packet-loopback APIs, plus a robot SDK
  that uses I2C sensors and CAN motor commands.

The public SDK surfaces are in `include/c26_graphics.h`,
`include/c26_audio.h`, `include/c26_input.h`, `include/c26_block.h`,
`include/c26_fs.h`, and `include/c26_devices.h`.

## Build and run

Requirements:

- LLVM/Clang with `riscv64-unknown-elf` support.
- QEMU with the RISC-V system target and virtio block, GPU, input, and sound
  devices.
- Python 3 and Make.

```bash
make smoke          # Full headless hardware and interaction gate
make run            # Graphical QEMU desktop with host audio
make run-headless   # UART console and dummy audio backend
```

`make run` and `make run-headless` preserve the guest disk at
`build/c26.img`. Delete that image to start with a blank computer. Inside BASIC:

```text
] NEW
] 10 PRINT "HELLO TOMORROW"
] SAVE HELLO
] LOAD HELLO
] LIST
] RUN
```

QEMU's RISC-V virt machine defaults its MMIO transports to the legacy
interface. The supplied run targets select modern virtio-MMIO explicitly;
custom QEMU invocations must also pass:

```text
-global virtio-mmio.force-legacy=false
```

## Controls

- Arrow keys select a desktop application.
- Enter launches the selected application.
- Mouse movement and left-click move the pointer and select applications.
- BASIC accepts commands from either the graphical virtio keyboard or the
  serial terminal.
- The Files application lists persisted program names and sizes from C26FS.

## Backend boundaries

Block storage, GPU, keyboard, mouse, and PCM audio have real QEMU
emulated-device backends.
The I2C, CAN, and packet SDKs are deterministic in-kernel fabrics intended for
safe demos; they do not claim to communicate with physical buses or an external
network. The 3D and ray-tracing paths run on the RISC-V CPU, which keeps their
behavior available without a host graphics API.
