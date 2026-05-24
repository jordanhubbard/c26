# Agent Instructions

c26 is a freestanding RISC-V project. The target program must remain C and
assembly only; host scripts may be Python or Make for build and smoke testing.

Use non-interactive commands. Validate changes with:

```bash
make smoke
```

The initial platform is QEMU `virt` with UART output. Keep hardware features
demo-safe unless a real emulated device backend is added.
