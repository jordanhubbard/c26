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

## Remaining course (in priority order)

- **virtio-net with a minimal honest IP stack.** QEMU emulates virtio-net;
  ARP/ICMP/UDP plus a guest echo verified through user-net hostfwd retires
  the "TCP/IP" loopback label for real networking.
- **C26FS growth.** DELETE/RENAME in fs.c, BASIC, and the Files app; a
  free-sector map instead of append-only allocation; raise the 12-file/4 KiB
  limits.
- **User C programs (the cartridge port).** A stable SDK vector table and
  memory-map contract; load flat binaries from C26FS and run them. The
  biggest architectural step; deferred until the interactive machine has
  soaked.
- **Host-side unit tests.** Compile the BASIC expression evaluator and C26FS
  layout logic on the host so parser and filesystem bugs are caught without a
  QEMU boot.
- **BASIC strings and a full-screen editor.** `A$` variables and C64-style
  in-place line editing on the console.

Principles that bound all of it: freestanding C and assembly only in the
target; a hobbyist can read the whole system; a capability is called
hardware-backed only when QEMU emulates the device; `make smoke` stays the
single gate.
