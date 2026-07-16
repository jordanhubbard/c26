/* PAINT: the first c26 cartridge. Mouse draws, 1-8 pick a color, C clears,
 * Q or Esc exits. Everything reaches the machine through the c26_api_t
 * vector table — no kernel symbols, no libc. */

#include "c26_api.h"

#define CANVAS_X 8
#define CANVAS_Y 40
#define CANVAS_W 624
#define CANVAS_H 432
#define BRUSH 3

static const uint32_t palette[8] = {
    0xffffff, 0x883932, 0x67b6bd, 0x55a049,
    0xbfce72, 0xb86962, 0x7869c4, 0xffad45,
};

static void draw_chrome(const c26_api_t *api, unsigned int selected)
{
    api->fill_rect(0, 0, 640, 32, 0x222957);
    api->text(8, 8, "C26 PAINT", 0xffffff, 0x222957, 2);
    api->text(340, 12, "1-8 COLOR  C CLEAR  Q QUIT", 0x9df6ff, 0x222957, 1);
    for (unsigned int i = 0; i < 8; i++) {
        int x = 160 + (int)i * 20;
        api->fill_rect(x, 6, 16, 20, palette[i]);
        if (i == selected) {
            api->draw_rect(x - 2, 4, 20, 24, 0xffffff);
        }
    }
    api->draw_rect(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_H + 2,
                   0x6570bd);
}

static void clear_canvas(const c26_api_t *api)
{
    api->fill_rect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, 0x0b1025);
}

int app_main(const c26_api_t *api)
{
    if (api->version != C26_CART_VERSION) {
        return 1;
    }
    api->puts("PAINT CART ONLINE\n");
    unsigned int selected = 0;
    api->fill_rect(0, 0, 640, 480, 0x000000);
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
        if ((buttons & 1) != 0 && x >= CANVAS_X && y >= CANVAS_Y &&
            x < CANVAS_X + CANVAS_W - BRUSH && y < CANVAS_Y + CANVAS_H - BRUSH) {
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
