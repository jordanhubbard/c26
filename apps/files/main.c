/* FILES: the windowed C26FS browser and launcher. Arrows or J/K move,
 * Enter/R runs the selection as a cartridge, E opens it in EDIT (the
 * filename travels over IPC), D deletes, Q quits. Clicking a row selects
 * it; clicking the selected row launches it. */

#include "ui.h"

#define MAX_ROWS 16
#define LIST_Y 26

static c26_ui_t ui;
static int selected;
static int top;
static int count;

static void format_row(char *label, const char *name, uint32_t size)
{
    int used = 0;
    while (*name != '\0' && used < 16) label[used++] = *name++;
    while (used < 18) label[used++] = ' ';
    char digits[12];
    int digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + size % 10);
        size /= 10;
    } while (size != 0);
    while (digit_count != 0) label[used++] = digits[--digit_count];
    label[used++] = ' ';
    label[used++] = 'B';
    label[used] = '\0';
}

static void redraw(const c26_api_t *api, const char *status, uint32_t color)
{
    ui_clear(&ui);
    ui_titlebar(&ui, "FILES", "R RUN  E EDIT  D DEL  Q QUIT");
    count = (int)api->fs_count();
    if (selected >= count) selected = count > 0 ? count - 1 : 0;
    if (selected < top) top = selected;
    if (selected >= top + MAX_ROWS) top = selected - MAX_ROWS + 1;
    for (int row = 0; row < MAX_ROWS && top + row < count; row++) {
        const char *name;
        uint32_t size;
        if (!api->fs_entry((size_t)(top + row), &name, &size)) continue;
        char label[40];
        format_row(label, name, size);
        if (ui_row(&ui, LIST_Y + row * 14, label, top + row == selected)) {
            selected = top + row;
        }
    }
    if (count == 0) {
        ui_text(&ui, 10, LIST_Y, "(NO FILES)", UI_WARN);
    }
    ui_status(&ui, status, color);
}

static int selected_name(const c26_api_t *api, char *name_out)
{
    const char *name;
    uint32_t size;
    if (!api->fs_entry((size_t)selected, &name, &size)) return 0;
    int i = 0;
    while (name[i] != '\0' && i < 15) {
        name_out[i] = name[i];
        i++;
    }
    name_out[i] = '\0';
    return 1;
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    ui_init(&ui, api);
    api->puts("FILES CART ONLINE\n");
    api->puts("FILES SEES ");
    api->put_int((int64_t)api->fs_count());
    api->puts(" ENTRIES\n");
    redraw(api, "READY", UI_TEXT);

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) return 0;
        int previous = selected;
        if (key == 'Q' || key == 0x1b) return 0;
        if (key == 'J' || key == C26_KEY_DOWN) selected++;
        if (key == 'K' || key == C26_KEY_UP) selected--;
        if (selected < 0) selected = 0;
        if (key == 'R' || key == '\n') {
            char name[16];
            if (selected_name(api, name)) {
                if (api->spawn(name) < 0) {
                    redraw(api, "NOT A CARTRIDGE", UI_WARN);
                } else {
                    redraw(api, "LAUNCHED", UI_GOOD);
                }
            }
        } else if (key == 'E') {
            char name[16];
            if (selected_name(api, name)) {
                int editor = api->spawn("EDIT");
                if (editor >= 0) {
                    int length = 0;
                    while (name[length] != '\0') length++;
                    api->send(editor, name, (size_t)length);
                    redraw(api, "EDITING", UI_GOOD);
                } else {
                    redraw(api, "EDIT NOT INSTALLED", UI_WARN);
                }
            }
        } else if (key == 'D') {
            char name[16];
            if (selected_name(api, name)) {
                api->fs_delete(name);
                redraw(api, "DELETED", UI_WARN);
            }
        } else if (key >= 0 || ui.clicked || selected != previous) {
            redraw(api, "READY", UI_TEXT);
        }
        ui_flush(&ui);
    }
}
