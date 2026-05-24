# c26

c26 is a standalone RISC-V reimagining of the Commodore 64 as if it shipped in
2026. It keeps the friendly retro first impression, but targets QEMU RISC-V
`virt` first and models a modern hobby computer: high-resolution graphics,
sound, USB, I2C, CAN, TCP/IP, and robot-control SDK concepts.

The first demo is intentionally a vertical slice. It is written in freestanding
C and assembly, boots directly under QEMU without a host OS, prints a retro
desktop, runs a small BASIC program, initializes emulated device APIs, and runs
a robot SDK sample.

## Build

Requirements:

- Homebrew LLVM or another Clang with `riscv64-unknown-elf` support.
- `qemu-system-riscv64`.
- `make` and `python3`.

Commands:

```bash
make build
make smoke
make run
```

`make smoke` builds `build/c26.elf`, boots it in QEMU, captures the UART log,
and checks for the c26 desktop, BASIC, device, and robot demo markers.

## Demo Markers

The smoke test expects:

- `C26 RISC-V HOME COMPUTER`
- `C26 DESKTOP`
- `BASIC READY`
- `ROBOT SDK DEMO`
- `C26 DEMO COMPLETE`

## Feedback Request

After running the demo, please send feedback on:

- Whether the BASIC prompt feels friendly and retro.
- Whether the desktop feels like a 2026 homage rather than a museum clone.
- Whether the device APIs make sense for USB, I2C, CAN, TCP/IP, and robots.
- Which robot-control examples should come first.
