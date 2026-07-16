#ifndef C26_CONSOLE_H
#define C26_CONSOLE_H

#include <stdint.h>

#define C26_CONSOLE_COLS 100U
#define C26_CONSOLE_ROWS 45U

typedef enum {
    C26_SCREEN_CONSOLE = 0,
    C26_SCREEN_DESKTOP = 1,
    C26_SCREEN_GFX = 2,
    C26_SCREEN_CART = 3,
} c26_screen_mode_t;

void c26_console_clear(void);
void c26_console_putc(char ch);
void c26_console_render(void);
int c26_console_dirty(void);
void c26_console_flush(void);

c26_screen_mode_t c26_screen_mode(void);
void c26_screen_set_mode(c26_screen_mode_t mode);

#endif
