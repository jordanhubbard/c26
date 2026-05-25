# c26 Demo Story

This document describes the final fleet demo for the c26 project.

## Slack/Demo Flow

The demo begins with users interacting in Slack to observe real-time system status and logs.

## BASIC Variable Demo

The small BASIC program runs automatically on boot. It demonstrates variable assignment, arithmetic, and output.

## User Commands

Users can run the following commands from the c26 repository root:

```bash
make build      # Build the c26 ELF and supporting binaries
make smoke      # Run the smoke test including the BASIC variable demo
make run        # Launch the QEMU emulator with the RISC-V ELF
```

Expected behavior:

1. QEMU boots the standalone RISC-V ELF.
2. The UART log shows the c26 retro desktop.
3. The BASIC program runs and prints computed output.
4. Graphics, audio, USB, I2C, CAN, TCP/IP, and robot SDK demo lines appear.
5. The smoke test reports `c26 smoke passed`.

Please share feedback in Slack on the BASIC interaction, desktop feel, device API names, and which robot SDK examples should come next.
