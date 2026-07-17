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

static void draw_chrome(const c26_api_t *api, unsigned int selected)
{
    api->fill_rect(0, 0, (int)width, CANVAS_Y - 2, 0x222957);
    api->text(4, 4, "PAINT", 0xffffff, 0x222957, 2);
    for (unsigned int i = 0; i < 8; i++) {
        int x = 80 + (int)i * 20;
        api->fill_rect(x, 3, 16, 18, palette[i]);
        if (i == selected) {
            api->draw_rect(x - 2, 1, 20, 22, 0xffffff);
        }
    }
    api->text((int)width - 84, 8, "C CLR Q QUIT", 0x9df6ff, 0x222957, 1);
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
