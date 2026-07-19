#ifndef C26_API_H
#define C26_API_H

/* The c26 cartridge ABI, version 1.
 *
 * A cartridge is a flat RV64 binary stored in C26FS, linked at
 * C26_CART_BASE, beginning with c26_cart_header_t. The kernel loads it,
 * zeroes .bss, and calls the entry as int app_main(const c26_api_t *api).
 * The return value is reported to the console. Version 1 is cooperative and
 * unprotected: the app runs in M-mode and must poll api->stop_requested().
 * Fields are only ever appended; the version bumps when semantics change.
 */

#include <stddef.h>
#include <stdint.h>

#define C26_CART_MAGIC 0x54524143U /* "CART" */
#define C26_CART_VERSION 2U
#define C26_CART_BASE 0x88000000UL
#define C26_CART_MAX_BYTES (2U * 1024U * 1024U)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t load_size;    /* bytes in the file image, header included */
    uint32_t bss_size;     /* zeroed by the loader after the image */
    uint32_t entry_offset; /* entry point relative to C26_CART_BASE */
    uint32_t reserved[3];
} c26_cart_header_t;

typedef struct {
    uint32_t version;

    /* Console (mirrored to UART and the text console). */
    void (*puts)(const char *text);
    void (*putc)(char ch);
    void (*put_int)(int64_t value);

    /* Input. getchar returns an ASCII code or -1; mouse reports the
       machine pointer and button bit 0 = left. */
    int (*getchar)(void);
    void (*mouse)(int *x, int *y, int *buttons);
    int (*stop_requested)(void);

    /* Time. ticks run at 100 Hz; idle pumps I/O and audio, then waits
       for the next interrupt — call it every loop iteration. */
    uint64_t (*ticks)(void);
    void (*idle)(void);

    /* Graphics: direct 1280x960 BGRX framebuffer plus primitives.
       present() pushes the framebuffer to the display. */
    uint32_t *(*framebuffer)(void);
    void (*pixel)(int x, int y, uint32_t color);
    void (*fill_rect)(int x, int y, int w, int h, uint32_t color);
    void (*draw_rect)(int x, int y, int w, int h, uint32_t color);
    void (*line)(int x0, int y0, int x1, int y1, uint32_t color);
    void (*text)(int x, int y, const char *message, uint32_t fg, uint32_t bg,
                 unsigned int scale);
    void (*present)(void);

    /* Audio: 8 voices; wave 0 square, 1 saw, 2 triangle, 3 noise. */
    int (*voice_start)(unsigned int voice, int wave, uint32_t frequency_hz,
                       uint8_t volume, uint8_t pan);
    void (*voice_stop)(unsigned int voice);

    /* Storage (C26FS). */
    int (*fs_save)(const char *name, const void *data, size_t size);
    int (*fs_load)(const char *name, void *data, size_t capacity,
                   size_t *size);
    int (*fs_delete)(const char *name);
    size_t (*fs_count)(void);
    int (*fs_entry)(size_t index, const char **name, uint32_t *size);

    /* Device fabric registers. */
    int (*dev_read8)(uint16_t reg, uint8_t *value);
    int (*dev_write8)(uint16_t reg, uint8_t value);

    /* --- version 2: windows and IPC --- */

    /* The app draws into [0,w) x [0,h) of its surface; the compositor shows
       that region in a movable window. Mouse coordinates are window-local. */
    void (*window_size)(uint32_t *width, uint32_t *height);

    /* Nonblocking message passing between jobs. send returns 0 when the
       target is gone or its mailbox is full; recv returns the byte count
       or -1 when the mailbox is empty (from may be NULL). */
    int (*send)(int job, const void *data, size_t size);
    int (*recv)(int *from, void *data, size_t capacity);

    /* Launch another cartridge from C26FS; returns its job number or -1.
       The new job takes focus. */
    int (*spawn)(const char *name);

    /* UDP over the real virtio-net device (QEMU user network: this
       machine is 10.0.2.15, the gateway 10.0.2.2). All nonblocking;
       udp_recv returns the byte count or -1 when nothing is queued. */
    int (*udp_bind)(uint16_t port);
    int (*udp_send)(uint32_t ip, uint16_t dst_port, uint16_t src_port,
                    const void *data, size_t size);
    int (*udp_recv)(uint16_t port, uint32_t *from_ip, uint16_t *from_port,
                    void *data, size_t capacity);

    /* --- version 3: the shared clipboard --- */

    /* clip_set copies up to 256 bytes into the one system clipboard; clip_get
       copies up to `capacity` bytes out and returns the byte count. Any app,
       or BASIC via CLIP/PASTE, reads what another wrote — copy/paste that
       crosses the app boundary. */
    int (*clip_set)(const void *data, size_t size);
    int (*clip_get)(void *data, size_t capacity);

    /* --- version 4: real-time clock --- */

    /* Wall-clock seconds since the Unix epoch, from the goldfish RTC. */
    uint64_t (*rtc_seconds)(void);
} c26_api_t;

/* Arrow keys are delivered through getchar() as these codes. */
#define C26_KEY_UP 0x1c
#define C26_KEY_DOWN 0x1d
#define C26_KEY_RIGHT 0x1e
#define C26_KEY_LEFT 0x1f

#endif
