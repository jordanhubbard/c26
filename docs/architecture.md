# c26 Architecture

## Target

c26 initially targets QEMU's `virt` RISC-V machine with a 64-bit `rv64imac`
kernel. The image is freestanding: assembly sets the stack and enters C, and no
host operating system services are used.

## Boot Contract

- QEMU loads `build/c26.elf` with `-bios none -kernel build/c26.elf`.
- `_start` in `src/boot.S` sets `sp` to `_stack_top`.
- `kmain` in `src/kernel.c` owns the runtime after boot.
- UART0 at `0x10000000` is the first console and smoke-test transport.

## Memory Map

- `0x80000000`: kernel text/data/bss.
- `0x10000000`: 16550-compatible UART on QEMU virt.
- Future milestones can add virtio-mmio for block/network devices and PLIC/CLINT
  interrupt support.

## First Demo Scope

The first demo is a vertical slice:

- Retro desktop banner and application launcher.
- BASIC scripted program with `PRINT`, `LET`, arithmetic, and `PEEK/POKE`
  hardware stubs.
- Graphics and audio HAL calls that are QEMU-safe and deterministic.
- USB, I2C, CAN, and TCP/IP API contracts as emulated C stubs.
- Robot SDK demo for motors and sensors.

## Non-Goals For The First Demo

- Full BASIC compatibility.
- Production USB/CAN/TCP/IP stacks.
- Real hardware board support.
- Persistent storage.

The goal is to prove the architecture and project workflow before widening the
emulated hardware surface.
