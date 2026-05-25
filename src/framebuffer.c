#include "c26.h"

// A simplified example structure for framebuffer
struct framebuffer {
    unsigned int width;
    unsigned int height;
    unsigned int* pixels;
};

static struct framebuffer fb;

void fb_init(unsigned int width, unsigned int height) {
    fb.width = width;
    fb.height = height;
    // In real system, allocate or map GPU framebuffer memory
    fb.pixels = (unsigned int*)0x10000000; // example pointer
    c26_puts("Framebuffer initialized\n");
}

void fb_draw_pixel(unsigned int x, unsigned int y, unsigned int color) {
    if (x < fb.width && y < fb.height) {
        fb.pixels[y * fb.width + x] = color;
    }
}

void fb_clear(unsigned int color) {
    for (unsigned int y = 0; y < fb.height; y++) {
        for (unsigned int x = 0; x < fb.width; x++) {
            fb_draw_pixel(x, y, color);
        }
    }
    c26_puts("Framebuffer cleared\n");
}

// Example main loop to demo framebuffer usage
void c26_run_desktop(void) {
    fb_init(640, 480);
    fb_clear(0x00000000); // Clear black
    c26_desktop_show();
}
