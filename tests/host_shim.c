#include "host_shim.h"
#include "c26.h"
#include "c26_audio.h"
#include "c26_block.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_graphics.h"

#include <stdlib.h>
#include <string.h>

char shim_output[65536];
size_t shim_output_length;
void (*shim_pump_hook)(char ch);

static const char *pending;
static uint64_t ticks;
static uint8_t device_registers[256];
static uint32_t pixels[C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT];
static c26_screen_mode_t mode;

uint8_t shim_disk[SHIM_DISK_SECTORS * 512];

void shim_output_reset(void)
{
    shim_output_length = 0;
    shim_output[0] = '\0';
}

void shim_pending_input(const char *text)
{
    pending = text;
}

void shim_disk_reset(void)
{
    memset(shim_disk, 0, sizeof(shim_disk));
}

/* Console/terminal */
void c26_putc(char ch)
{
    if (shim_output_length + 2 < sizeof(shim_output)) {
        shim_output[shim_output_length++] = ch;
        shim_output[shim_output_length] = '\0';
    }
}

void c26_puts(const char *text)
{
    while (text != 0 && *text != '\0') c26_putc(*text++);
}

void c26_put_uint(uint64_t value)
{
    char digits[21];
    size_t used = 0;
    if (value == 0) {
        c26_putc('0');
        return;
    }
    while (value != 0 && used < sizeof(digits)) {
        digits[used++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (used != 0) c26_putc(digits[--used]);
}

void c26_put_int(int64_t value)
{
    if (value < 0) {
        c26_putc('-');
        c26_put_uint((uint64_t)-value);
        return;
    }
    c26_put_uint((uint64_t)value);
}

void c26_put_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    c26_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        c26_putc(hex[(value >> shift) & 0xf]);
}

/* Parsing helpers (mirrors src/runtime.c, which defines memcpy/memset and
 * therefore cannot link against libc). */
const char *c26_skip_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t') text++;
    return text;
}

uint64_t c26_parse_uint(const char **cursor)
{
    const char *text = c26_skip_spaces(*cursor);
    uint64_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint64_t)(*text - '0');
        text++;
    }
    *cursor = text;
    return value;
}

int c26_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

/* Time and the I/O pump */
uint64_t c26_interrupt_ticks(void)
{
    return ticks;
}

void c26_idle(void)
{
    ticks++;
}

uint64_t c26_rtc_seconds(void)
{
    return 1752700000ULL; /* a fixed 2025 timestamp for deterministic tests */
}

void c26_io_pump(void)
{
    ticks++;
    if (shim_pump_hook != 0 && pending != 0) {
        while (*pending != '\0') shim_pump_hook(*pending++);
        pending = 0;
    }
}

/* Screen and desktop */
c26_screen_mode_t c26_screen_mode(void) { return mode; }
void c26_screen_set_mode(c26_screen_mode_t m) { mode = m; }
void c26_console_clear(void) {}
void c26_console_flush(void) {}
void c26_desktop_invalidate(void) {}
void c26_robot_demo(void) { c26_puts("ROBOT SDK DEMO\n"); }
int c26_cart_run(const char *name) { (void)name; return -1; }
int c26_cart_kill(int job) { (void)job; return 0; }
int c26_cart_move_window(int j, int x, int y) { (void)j;(void)x;(void)y; return 0; }
int c26_cart_focus(int j) { (void)j; return 0; }
int c26_cart_send(int j, const void *d, size_t s) { (void)j;(void)d;(void)s; return 0; }
void c26_cart_list_jobs(void) { c26_puts("  (NO JOBS)\n"); }
void c26_cart_focus_console(void) {}
void c26_cart_focus_next(void) {}
void c26_poweroff(void) { exit(0); }

/* Graphics */
uint32_t *c26_framebuffer_pixels(void) { return pixels; }
void c26_framebuffer_present(void) {}
void c26_draw_pixel(int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < (int)C26_SCREEN_WIDTH &&
        y < (int)C26_SCREEN_HEIGHT)
        pixels[(unsigned)y * C26_SCREEN_WIDTH + (unsigned)x] = color;
}
void c26_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++) c26_draw_pixel(px, py, color);
}
void c26_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    c26_fill_rect(x, y, w, 1, color);
    c26_fill_rect(x, y + h - 1, w, 1, color);
    c26_fill_rect(x, y, 1, h, color);
    c26_fill_rect(x + w - 1, y, 1, h, color);
}
void c26_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    (void)x1;
    (void)y1;
    c26_draw_pixel(x0, y0, color);
}
void c26_draw_text(int x, int y, const char *t, uint32_t fg, uint32_t bg,
                   unsigned int scale)
{
    (void)t; (void)fg; (void)bg; (void)scale;
    c26_draw_pixel(x, y, fg);
}

/* Audio */
int c26_audio_voice_start(unsigned int voice, c26_waveform_t wave,
                          uint32_t hz, uint8_t volume, uint8_t pan)
{
    (void)wave; (void)volume; (void)pan;
    return voice < C26_AUDIO_VOICE_COUNT && hz != 0 &&
           hz <= C26_AUDIO_SAMPLE_RATE / 2;
}
void c26_audio_voice_stop(unsigned int voice) { (void)voice; }

/* Device fabric */
int c26_device_write8(uint16_t reg, uint8_t value)
{
    if (reg >= sizeof(device_registers)) return 0;
    device_registers[reg] = value;
    return 1;
}
int c26_device_read8(uint16_t reg, uint8_t *value)
{
    if (reg >= sizeof(device_registers) || value == 0) return 0;
    *value = device_registers[reg];
    return 1;
}

/* Block device: an in-memory disk with the same geometry as make disk. */
int c26_block_online(void) { return 1; }
uint64_t c26_block_sector_count(void) { return SHIM_DISK_SECTORS; }
int c26_block_read(uint64_t sector, void *buffer)
{
    if (sector >= SHIM_DISK_SECTORS) return 0;
    memcpy(buffer, shim_disk + sector * 512, 512);
    return 1;
}
int c26_block_write(uint64_t sector, const void *buffer)
{
    if (sector >= SHIM_DISK_SECTORS) return 0;
    memcpy(shim_disk + sector * 512, buffer, 512);
    return 1;
}
int c26_block_flush(void) { return 1; }
