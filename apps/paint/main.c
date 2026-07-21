/* PAINT: mouse draws, 1-8 pick a color, C clears, Q or Esc exits. Lays
 * itself out to whatever window the compositor gave it (ABI v2) and talks
 * to the machine only through the c26_api_t vector table. */

#include "c26_api.h"

#define BRUSH 3
#define CANVAS_Y 26

static const uint32_t palette[8] = {
    0xffffff, 0x883932, 0x67b6bd, 0x55a049,
    0xbfce72, 0xb86962, 0x7869c4, 0xffad45,
};

static uint32_t width;
static uint32_t height;

static uint32_t cmix(uint32_t a, uint32_t b, int t)
{
    int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
    int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return ((uint32_t)(ra + (rb - ra) * t / 256) << 16) |
           ((uint32_t)(ga + (gb - ga) * t / 256) << 8) |
           (uint32_t)(ba + (bb - ba) * t / 256);
}
static void vgrad(const c26_api_t *api, int x, int y, int w, int h, uint32_t top,
                  uint32_t bot)
{
    for (int i = 0; i < h; i++)
        api->fill_rect(x, y + i, w, 1, cmix(top, bot, i * 256 / (h > 0 ? h : 1)));
}
static void label_fg(const c26_api_t *api, int x, int y, const char *s,
                     uint32_t fg, uint32_t bg, int scale)
{
    if (api->version >= 6)
        api->text_fg(x, y, s, fg, (unsigned int)scale);
    else
        api->text(x, y, s, fg, bg, scale);
}

static void draw_chrome(const c26_api_t *api, unsigned int selected)
{
    /* glossy title + tool bar */
    vgrad(api, 0, 0, (int)width, CANVAS_Y - 2, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x4a56b4);            /* top highlight */
    api->fill_rect(0, CANVAS_Y - 3, (int)width, 1, 0x0a0e20); /* seam shadow */
    label_fg(api, 4, 4, "PAINT", 0xffffff, 0x30397e, 2);
    for (unsigned int i = 0; i < 8; i++) {
        int x = 80 + (int)i * 20;
        api->fill_rect(x, 3, 16, 18, palette[i]);
        /* subtle bevel: bright top edge, dark bottom edge; colour unchanged */
        api->fill_rect(x, 3, 16, 1, cmix(palette[i], 0xffffff, 90));
        api->fill_rect(x, 3 + 18 - 1, 16, 1, cmix(palette[i], 0x000000, 90));
        if (i == selected) {
            api->draw_rect(x - 2, 1, 20, 22, 0xffffff);
        }
    }
    label_fg(api, (int)width - 84, 8, "C CLR Q QUIT", 0x9df6ff, 0x30397e, 1);
}

static void clear_canvas(const c26_api_t *api)
{
    api->fill_rect(0, CANVAS_Y, (int)width, (int)height - CANVAS_Y, 0x0b1025);
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) {
        return 1;
    }
    api->window_size(&width, &height);
    api->puts("PAINT CART ONLINE\n");
    unsigned int selected = 0;
    clear_canvas(api);
    draw_chrome(api, selected);
    api->present();

    uint64_t last_present = 0;
    int dirty = 0;
    for (;;) {
        if (api->stop_requested()) {
            break;
        }
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) {
            break;
        }
        if (ch >= '1' && ch <= '8') {
            selected = (unsigned int)(ch - '1');
            draw_chrome(api, selected);
            dirty = 1;
        }
        if (ch == 'C') {
            clear_canvas(api);
            dirty = 1;
        }
        int x;
        int y;
        int buttons;
        api->mouse(&x, &y, &buttons);
        if ((buttons & 1) != 0 && x >= 0 && y >= CANVAS_Y &&
            x < (int)width - BRUSH && y < (int)height - BRUSH) {
            api->fill_rect(x, y, BRUSH, BRUSH, palette[selected]);
            dirty = 1;
        }
        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 3) {
            last_present = now;
            dirty = 0;
            api->present();
        }
        api->idle();
    }
    api->puts("PAINT CART EXIT\n");
    return 0;
}
