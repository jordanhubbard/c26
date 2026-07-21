#ifndef C26_GRAPHICS_H
#define C26_GRAPHICS_H

#include <stdint.h>

/* Native scanout resolution. The kernel keeps a full-screen framebuffer plus
   one per-process compositing surface (see SURFACE_PIXELS in cart.c); at
   2560x1440x32 that is ~15 MiB each, and the kernel's static surfaces total
   ~74 MiB — still under the 128 MiB below the cartridge base at 0x88000000. */
#define C26_SCREEN_WIDTH 2560U
#define C26_SCREEN_HEIGHT 1440U

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t color;
} c26_gl_vertex_t;

int c26_framebuffer_init(void);
uint32_t *c26_framebuffer_pixels(void);
unsigned int c26_framebuffer_width(void);
unsigned int c26_framebuffer_height(void);
const char *c26_framebuffer_backend(void);
void c26_framebuffer_present(void);

void c26_draw_pixel(int x, int y, uint32_t color);
void c26_fill_rect(int x, int y, int width, int height, uint32_t color);
void c26_draw_rect(int x, int y, int width, int height, uint32_t color);
void c26_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
/* 5-column glyph bitmap for any printable ASCII char, bit 0 = top row. */
const uint8_t *c26_font_glyph(char ch);
void c26_draw_char(int x, int y, char ch, uint32_t foreground,
                   uint32_t background, unsigned int scale);
void c26_draw_text(int x, int y, const char *text, uint32_t foreground,
                   uint32_t background, unsigned int scale);

void c26_gl_clear(uint32_t color);
void c26_gl_clear_depth(void);
void c26_gl_draw_triangle(c26_gl_vertex_t a, c26_gl_vertex_t b,
                          c26_gl_vertex_t c);
void c26_gl_demo_cube(int origin_x, int origin_y, unsigned int scale);
void c26_raytrace_demo(int x, int y, int width, int height);
void c26_graphics_render_demo(void);

#endif
