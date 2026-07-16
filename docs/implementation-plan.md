# c26 Implementation Record

The original implementation plan described a UART-only vertical slice. The
abandoned stories were completed on 2026-07-16 and are tracked in Beads; this
document records the delivered boundaries and verification evidence rather than
serving as a second task tracker.

## Delivered story mapping

| Original area | Delivered result | Primary files | Acceptance signal |
| --- | --- | --- | --- |
| Runtime/kernel | Freestanding boot plus continuous device/input/audio event loop | `boot.S`, `kernel.c`, `runtime.c` | `C26 INTERACTIVE LOOP ONLINE` |
| BASIC | Scripted startup and live UART/virtio command entry | `basic.c` | Injected `DEVICE WRITE/READ` returns 99 |
| Desktop | Real framebuffer UI, selection, pointer and application launch actions | `desktop.c`, `framebuffer.c`, `input.c` | GPU plus two input devices online |
| Graphics | Drawing API, software 3D rasterizer, z-buffer and ray tracer | `graphics.c`, `c26_graphics.h` | GL and ray-render markers |
| Audio | Eight-voice mixer and real virtio-sound PCM output | `audio.c`, `c26_audio.h` | PCM stream online and DSP checksum |
| Device fabric | Register, I2C, CAN and packet APIs with bounded state | `devices.c`, `c26_devices.h` | Register and loopback readbacks |
| Robot SDK | I2C sensor sampling and CAN motor command publication | `robot.c` | Stateful control path marker |
| User workflow | Graphical, headless and strict smoke targets | `Makefile`, `scripts/smoke.py`, `README.md` | `make smoke` |

## Verification

The required project gate is:

```bash
make smoke
```

It launches all implemented QEMU backends with modern virtio-MMIO, supplies
commands through the live serial input path, and fails if any required hardware,
rendering, audio, SDK, BASIC, robot, or interaction marker is absent.
