# c26 Demo Story

The demo now follows a complete home-computer workflow rather than a startup
transcript.

## Run it

```bash
make smoke      # Automated two-boot persistence and hardware gate
make run        # Graphical desktop; build/c26.img survives restarts
```

## Human flow

1. QEMU boots the freestanding RV64 image and reports CLINT, PLIC, virtio block,
   GPU, input, and sound devices online.
2. The graphical desktop appears. Select **FILES** to see the C26FS browser.
3. In BASIC, enter a program and persist it:

   ```text
   ] NEW
   ] 10 PRINT "C26 REMEMBERS"
   ] 20 PRINT 20+6
   ] LIST
   ] RUN
   ] SAVE DEMO
   ] DIR
   ```

4. Restart `make run`, then enter `LOAD DEMO` and `RUN`. The program comes from
   the virtio disk, not retained RAM.
5. Select **FILES** again; `DEMO` and its byte size are visible in the desktop.

The startup still demonstrates graphics, ray tracing, PCM sound, device
registers, I2C, CAN, packet loopback, and the robot SDK. The new persistence
path turns those facilities into an interrupt-driven computer users can program
and return to later.
