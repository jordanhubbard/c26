# c26 Roadmap

The first-principles goal: a home computer is a tight loop — you type on the
machine, the machine's own screen answers, and a few lines of the built-in
language make the hardware do something visible and audible. The 2026-07
"interactive machine" milestone closed that loop:

1. Full printable-ASCII font and a scrolling framebuffer text console; all
   system and BASIC output renders on the machine's own display and mirrors
   to UART for automation.
2. Screen modes and app lifecycle: boot to BASIC, Esc to the desktop
   launcher, `SCREEN 1` graphics mode.
3. BASIC V3: real expressions, control flow, `INPUT`/`GET`/`PAUSE`, and
   hardware statements wrapping the graphics, sound, and device SDKs.
4. A self demo: a fresh machine installs `DEMO` — written in BASIC, drawing
   and playing sound through the same statements users type.
5. Smoke v2 gates all of it headlessly, including the two-process disk
   persistence proof.

## Delivered: M2 — the cartridge port (2026-07-17)

C26FS v2 (64 files, 128 KiB, free-sector bitmap, DELETE/RENAME), the frozen
`c26_api.h` cartridge ABI, the loader, host-side installation via
`scripts/fsinstall.py`, and `apps/paint` as the first out-of-tree program.
Version 1 is cooperative and unprotected by design.

## Delivered: M3 — protection (2026-07-17)

Cartridges now run in U-mode inside per-app Sv39 address spaces built by
src/vm.c, reached only through the 25-syscall surface in c26_user.h (the
c26_api_t contract is unchanged — its pointers land in a user-mapped stub
page). The M-mode kernel keeps ownership on every interrupt: input and audio
survive a spinning app, Ctrl-C kills it, and a wild pointer faults the app
while the machine keeps answering. apps/crash and apps/spin prove both in
the smoke gate. Single-process by design at this stage.

## Remaining course (in priority order)

- **M4 — Compositor + multiprocessing.** Per-app shared-memory surfaces,
  z-order, focus routing — and with multiple live apps, the scheduler and
  IPC deferred from M3.
- **M5 — UI toolkit.** c26_ui widgets/event loop; Files, terminal, and a
  text editor become windowed apps.
- **M6 — App suite + networking.** virtio-net with a minimal honest IP
  stack, then editor, paint, tracker, network client, robot panel, a game.
- **M7 — Self-hosting + scriptable desktop.** On-machine editor and
  assembler/tiny-C producing runnable cartridges; BASIC bindings into the
  toolkit; the experiment writeup (LOC budget vs capability).
- **Continuous.** Host-side unit tests for basic.c/fs.c logic; BASIC strings
  and a full-screen editor.

Principles that bound all of it: freestanding C and assembly only in the
target; a hobbyist can read the whole system; a capability is called
hardware-backed only when QEMU emulates the device; `make smoke` stays the
single gate.
