#include "ui.h"

#define ROW_HEIGHT 26

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
    ui->api->fill_rect(0, 0, (int)ui->width, 30, UI_PANEL);
    ui->api->text(8, 5, title, UI_BRIGHT, UI_PANEL, 3);
    if (hint != 0) {
        int x = (int)ui->width - 6 * (int)0;
        (void)x;
        int length = 0;
        while (hint[length] != '\0') length++;
        ui->api->text((int)ui->width - 12 * length - 8, 9, hint, UI_TEXT,
                      UI_PANEL, 2);
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
    ui->api->fill_rect(0, y, (int)ui->width, 24, UI_PANEL);
    ui->api->text(8, y + 5, text, color, UI_PANEL, 2);
    ui->dirty = 1;
}

int ui_hit(const c26_ui_t *ui, int x, int y, int w, int h)
{
    return ui->clicked && ui->mouse_x >= x && ui->mouse_x < x + w &&
           ui->mouse_y >= y && ui->mouse_y < y + h;
}

int ui_row(c26_ui_t *ui, int y, const char *text, int selected)
{
    uint32_t bg = selected ? UI_ACCENT : UI_BG;
    ui->api->fill_rect(4, y, (int)ui->width - 8, ROW_HEIGHT, bg);
    ui->api->text(12, y + 5, text, selected ? UI_BRIGHT : UI_TEXT, bg, 2);
    ui->dirty = 1;
    return ui_hit(ui, 4, y, (int)ui->width - 8, ROW_HEIGHT);
}
