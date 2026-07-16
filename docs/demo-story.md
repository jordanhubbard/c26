# c26 Demo Story

The demo is the machine itself: it boots to its own console, and its
showcase program is written in its own BASIC and stored on its own disk.

## Run it

```bash
make smoke      # Automated hardware, language, and two-boot persistence gate
make run        # Graphical console; build/c26.img survives restarts
```

## Human flow

1. QEMU boots the freestanding RV64 image; boot messages report CLINT, PLIC,
   virtio block, GPU, input, and sound online, then the display becomes the
   BASIC console with a `READY` prompt.
2. Run the self demo the machine installed on its first boot:

   ```text
   ] LOAD DEMO
   ] RUN
   ```

   The screen switches to graphics mode, draws a tunnel of rectangles, plays
   a rising scale through the virtio-sound PCM stream, and returns to the
   console.
3. Write and keep a program of your own:

   ```text
   ] NEW
   ] 10 INPUT N
   ] 20 FOR I=1 TO N
   ] 30 PRINT I*I
   ] 40 NEXT
   ] RUN
   ] SAVE SQUARES
   ] DIR
   ```

4. Press Esc for the desktop launcher; the Files application shows `DEMO` and
   `SQUARES` with their byte sizes straight from C26FS. Esc returns to BASIC.
5. Restart `make run`, then `LOAD SQUARES` and `RUN`. The program comes from
   the virtio disk, not retained RAM.

Everything the boot banner claims — graphics, ray tracing, PCM sound, device
registers, I2C, CAN, packet loopback, the robot SDK — is reachable from the
keyboard: `SCREEN 1`, `PLOT`, `LINE`, `RECT`, `TEXT`, `SOUND`, `DEVICE READ`,
`DEVICE WRITE`, and `ROBOT` drive the same SDKs the C code uses.
