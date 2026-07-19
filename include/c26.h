#ifndef C26_H
#define C26_H

#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#include <string.h> /* host test builds use libc */
#else
void *memcpy(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
#endif

void c26_uart_putc(char ch);
int c26_uart_getc_nonblocking(void);
void c26_uart_enable_interrupt(void);
void c26_uart_handle_interrupt(void);
void c26_putc(char ch);
void c26_puts(const char *text);
void c26_put_uint(uint64_t value);
void c26_put_int(int64_t value);
void c26_put_hex(uint64_t value);

int c26_starts_with(const char *text, const char *prefix);
const char *c26_skip_spaces(const char *text);
uint64_t c26_parse_uint(const char **cursor);

void c26_desktop_init(void);
void c26_desktop_poll(void);
void c26_desktop_invalidate(void);
void c26_io_pump(void);
void c26_basic_init(void);
void c26_basic_feed_char(char ch);
int c26_basic_running(void);
int c26_basic_queue_consumed_externally(void);
int c26_basic_can_accept(void);
int c26_basic_key_pop(char *ch);
int c26_basic_break_requested(void);
void c26_basic_clear_break(void);
void c26_basic_set_external_consumer(int on);
void c26_desktop_mouse(int *x, int *y, int *buttons);
int c26_cart_run(const char *name);
void c26_scheme_enter(void);
void c26_scheme_leave(void);
int c26_scheme_feed(const char *line);
void c26_cart_schedule(void);
int c26_cart_any_runnable(void);
int c26_cart_kill(int job);
int c26_cart_move_window(int job, int x, int y);
int c26_cart_resize_window(int job, int w, int h);
int c26_cart_set_minimized(int job, int minimized);
int c26_cart_focus(int job);

/* The dock: a launcher bar of app tiles, rebuilt from C26FS at boot. */
void c26_dock_rebuild(void);
void c26_dock_print(void);

/* The system clipboard: one shared text buffer for copy/paste across apps. */
void c26_clipboard_set(const char *data, unsigned int len);
unsigned int c26_clipboard_get(char *buf, unsigned int cap);
unsigned int c26_clipboard_length(void);
int c26_cart_send(int job, const void *data, size_t size);
void c26_cart_list_jobs(void);
void c26_cart_focus_console(void);
void c26_cart_focus_next(void);
void c26_compositor_flush(void);
void c26_compositor_mark_dirty(void);
int c26_wm_click(int x, int y, int pressed);
void c26_wm_pointer_moved(int x, int y);

/* Synthetic pointer input (BASIC CLICK/DRAG; the smoke gate drives the WM). */
void c26_desktop_inject_pointer(int x, int y);
void c26_desktop_inject_button(int pressed);
void c26_desktop_draw_pointer(void);
void c26_graphics_demo(void);
void c26_audio_demo(void);
void c26_audio_poll(void);
void c26_devices_demo(void);
void c26_robot_demo(void);

void c26_interrupts_init(void);
uint64_t c26_interrupt_ticks(void);
uint64_t c26_interrupt_external_count(void);
void c26_idle(void);
uint64_t c26_rtc_seconds(void);
void c26_poweroff(void) __attribute__((noreturn));

#endif
