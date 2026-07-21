#include "ui.h"

#define ROW_HEIGHT 26

/* Shading helpers so the toolkit chrome matches the desktop's Aqua look. */
static uint32_t ui_mix(uint32_t a, uint32_t b, int t)
{
    int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
    int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return ((uint32_t)(ra + (rb - ra) * t / 256) << 16) |
           ((uint32_t)(ga + (gb - ga) * t / 256) << 8) |
           (uint32_t)(ba + (bb - ba) * t / 256);
}
static void ui_vgrad(c26_ui_t *ui, int x, int y, int w, int h, uint32_t top,
                     uint32_t bot)
{
    if (h <= 0) return;
    for (int i = 0; i < h; i++)
        ui->api->fill_rect(x, y + i, w, 1, ui_mix(top, bot, i * 256 / h));
}
/* Box-free text where the ABI supports it (v6+), so labels sit on gradients. */
static void ui_glyphs(c26_ui_t *ui, int x, int y, const char *s, uint32_t fg,
                      uint32_t bg, unsigned int scale)
{
    if (ui->api->version >= 6)
        ui->api->text_fg(x, y, s, fg, scale);
    else
        ui->api->text(x, y, s, fg, bg, scale);
}

void ui_init(c26_ui_t *ui, const c26_api_t *api)
{
    ui->api = api;
    api->window_size(&ui->width, &ui->height);
    ui->mouse_x = -1;
    ui->mouse_y = -1;
    ui->mouse_buttons = 0;
    ui->clicked = 0;
    ui->dirty = 1;
    ui->last_present = 0;
}

int ui_poll(c26_ui_t *ui)
{
    const c26_api_t *api = ui->api;
    if (api->stop_requested()) {
        return -2;
    }
    int previous = ui->mouse_buttons;
    api->mouse(&ui->mouse_x, &ui->mouse_y, &ui->mouse_buttons);
    ui->clicked = (ui->mouse_buttons & 1) != 0 && (previous & 1) == 0;
    int key = api->getchar();
    if (key < 0) {
        api->idle();
    }
    return key;
}

void ui_flush(c26_ui_t *ui)
{
    uint64_t now = ui->api->ticks();
    if (ui->dirty && now - ui->last_present >= 3) {
        ui->last_present = now;
        ui->dirty = 0;
        ui->api->present();
    }
}

void ui_clear(c26_ui_t *ui)
{
    ui->api->fill_rect(0, 0, (int)ui->width, (int)ui->height, UI_BG);
    ui->dirty = 1;
}

void ui_titlebar(c26_ui_t *ui, const char *title, const char *hint)
{
    int w = (int)ui->width;
    ui_vgrad(ui, 0, 0, w, 30, 0x30397e, 0x191f48);
    ui->api->fill_rect(0, 0, w, 1, 0x4a56b4);  /* top highlight */
    ui->api->fill_rect(0, 29, w, 1, 0x0a0e20); /* seam shadow */
    ui_glyphs(ui, 8, 5, title, UI_BRIGHT, UI_PANEL, 3);
    if (hint != 0) {
        int length = 0;
        while (hint[length] != '\0') length++;
        ui_glyphs(ui, w - 12 * length - 8, 9, hint, UI_TEXT, UI_PANEL, 2);
    }
    ui->dirty = 1;
}

void ui_text(c26_ui_t *ui, int x, int y, const char *text, uint32_t color)
{
    ui->api->text(x, y, text, color, UI_BG, 2);
    ui->dirty = 1;
}

void ui_status(c26_ui_t *ui, const char *text, uint32_t color)
{
    int y = (int)ui->height - 24;
    int w = (int)ui->width;
    ui_vgrad(ui, 0, y, w, 24, 0x272f63, 0x161b3c);
    ui->api->fill_rect(0, y, w, 1, 0x3a4488); /* top hairline */
    ui_glyphs(ui, 8, y + 5, text, color, UI_PANEL, 2);
    ui->dirty = 1;
}

int ui_hit(const c26_ui_t *ui, int x, int y, int w, int h)
{
    return ui->clicked && ui->mouse_x >= x && ui->mouse_x < x + w &&
           ui->mouse_y >= y && ui->mouse_y < y + h;
}

int ui_row(c26_ui_t *ui, int y, const char *text, int selected)
{
    int w = (int)ui->width - 8;
    uint32_t bg = selected ? UI_ACCENT : UI_BG;
    if (selected) {
        ui_vgrad(ui, 4, y, w, ROW_HEIGHT, 0x4653b4, 0x2f3894);
        ui->api->fill_rect(4, y, w, 1, 0x5a67c8); /* top highlight */
    } else {
        ui->api->fill_rect(4, y, w, ROW_HEIGHT, bg);
    }
    ui_glyphs(ui, 12, y + 5, text, selected ? UI_BRIGHT : UI_TEXT, bg, 2);
    ui->dirty = 1;
    return ui_hit(ui, 4, y, w, ROW_HEIGHT);
}
