#ifndef C26_CONSOLE_H
#define C26_CONSOLE_H

#include <stdint.h>

#define C26_CONSOLE_COLS 106U
#define C26_CONSOLE_ROWS 43U
#define C26_CONSOLE_CELL_W 12
#define C26_CONSOLE_CELL_H 20

typedef enum {
    C26_SCREEN_CONSOLE = 0,
    C26_SCREEN_DESKTOP = 1,
    C26_SCREEN_GFX = 2,
    C26_SCREEN_CART = 3,
} c26_screen_mode_t;

void c26_console_clear(void);
void c26_console_putc(char ch);
void c26_console_render(void);
void c26_console_render_cells(void); /* to the framebuffer, no present */
/* Blit the console text into a window content area of max_w x max_h at (ox,oy),
   bottom-anchored so the prompt stays visible when the window is small. */
void c26_console_blit(int ox, int oy, int max_w, int max_h);
/* Content pixel size of the console text grid (for window sizing). */
int c26_console_pixel_width(void);
int c26_console_pixel_height(void);
int c26_console_dirty(void);
void c26_console_flush(void);

c26_screen_mode_t c26_screen_mode(void);
void c26_screen_set_mode(c26_screen_mode_t mode);

#endif
