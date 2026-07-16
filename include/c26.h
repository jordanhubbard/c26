#ifndef C26_H
#define C26_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);

void c26_uart_putc(char ch);
int c26_uart_getc_nonblocking(void);
void c26_uart_enable_interrupt(void);
void c26_uart_handle_interrupt(void);
void c26_puts(const char *text);
void c26_put_uint(uint64_t value);
void c26_put_hex(uint64_t value);

int c26_starts_with(const char *text, const char *prefix);
const char *c26_skip_spaces(const char *text);
uint64_t c26_parse_uint(const char **cursor);

void c26_desktop_show(void);
void c26_desktop_poll(void);
void c26_desktop_invalidate(void);
void c26_basic_demo(void);
void c26_basic_feed_char(char ch);
void c26_graphics_demo(void);
void c26_audio_demo(void);
void c26_audio_poll(void);
void c26_devices_demo(void);
void c26_robot_demo(void);

void c26_interrupts_init(void);
uint64_t c26_interrupt_ticks(void);
uint64_t c26_interrupt_external_count(void);
void c26_idle(void);

#endif
