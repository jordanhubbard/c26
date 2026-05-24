# c26 Demo Story

Please try the first c26 demo from a fresh checkout:

```bash
cd ~/Src/c26
make smoke
make run
```

Expected behavior:

1. QEMU boots a standalone RISC-V ELF.
2. The UART log shows the c26 retro desktop.
3. A tiny BASIC program runs and prints computed output.
4. Graphics, audio, USB, I2C, CAN, TCP/IP, and robot SDK demo lines appear.
5. The smoke test reports `c26 smoke passed`.

Please share feedback in Slack on the BASIC interaction, desktop feel, device
API names, and which robot SDK examples should come next.
