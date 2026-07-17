/* EDIT: the windowed text editor. If a filename arrives over IPC within
 * the first half second (FILES sends one), that file is opened; otherwise
 * it edits NOTES. Arrows (or Ctrl-P/N/B/F) move, printable keys insert,
 * Backspace deletes, Enter breaks lines, Ctrl-S saves, Ctrl-Q or Esc
 * quits. */

#include "ui.h"

#define BUFFER_MAX 8000
#define TEXT_Y 26
#define LINE_HEIGHT 12

static c26_ui_t ui;
static char text[BUFFER_MAX + 1];
static int length;
static int cursor;
static int top_line;
static char filename[16] = "NOTES";

static int line_start(int line)
{
    int position = 0;
    while (line > 0 && position < length) {
        if (text[position] == '\n') line--;
        position++;
    }
    return position;
}

static void cursor_place(int *line, int *column)
{
    *line = 0;
    *column = 0;
    for (int i = 0; i < cursor; i++) {
        if (text[i] == '\n') {
            (*line)++;
            *column = 0;
        } else {
            (*column)++;
        }
    }
}

static void redraw(const c26_api_t *api, const char *status, uint32_t color)
{
    int rows = ((int)ui.height - TEXT_Y - 16) / LINE_HEIGHT;
    int columns = ((int)ui.width - 12) / 6;
    int line;
    int column;
    cursor_place(&line, &column);
    if (line < top_line) top_line = line;
    if (line >= top_line + rows) top_line = line - rows + 1;

    ui_clear(&ui);
    char title[24];
    int used = 0;
    const char *prefix = "EDIT ";
    while (prefix[used] != '\0') {
        title[used] = prefix[used];
        used++;
    }
    for (int i = 0; filename[i] != '\0' && used < 22; i++) {
        title[used++] = filename[i];
    }
    title[used] = '\0';
    ui_titlebar(&ui, title, "^S SAVE  ^Q QUIT");

    int position = line_start(top_line);
    for (int row = 0; row < rows && position <= length; row++) {
        char visible[110];
        int visible_length = 0;
        while (position < length && text[position] != '\n' &&
               visible_length < columns && visible_length < 108) {
            visible[visible_length++] = text[position++];
        }
        visible[visible_length] = '\0';
        while (position < length && text[position] != '\n') position++;
        if (position < length) position++;
        api->text(6, TEXT_Y + row * LINE_HEIGHT, visible, UI_TEXT, UI_BG, 1);
        int this_line = top_line + row;
        if (this_line == line && column <= columns) {
            api->fill_rect(6 + column * 6,
                           TEXT_Y + row * LINE_HEIGHT + 9, 6, 2, UI_BRIGHT);
        }
    }
    ui_status(&ui, status, color);
    ui.dirty = 1;
}

static void insert(char ch)
{
    if (length >= BUFFER_MAX) return;
    for (int i = length; i > cursor; i--) text[i] = text[i - 1];
    text[cursor++] = ch;
    length++;
}

static void erase(void)
{
    if (cursor == 0) return;
    for (int i = cursor; i < length; i++) text[i - 1] = text[i];
    cursor--;
    length--;
}

static void move_line(int delta)
{
    int line;
    int column;
    cursor_place(&line, &column);
    int target = line + delta;
    if (target < 0) return;
    int start = line_start(target);
    if (start >= length && target > 0 && text[length - 1] != '\n') {
        return;
    }
    cursor = start;
    while (column > 0 && cursor < length && text[cursor] != '\n') {
        cursor++;
        column--;
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    ui_init(&ui, api);
    api->puts("EDIT CART ONLINE\n");

    /* A launcher may hand us a filename over IPC. */
    uint64_t deadline = api->ticks() + 50;
    while (api->ticks() < deadline) {
        char message[16];
        int received = api->recv(0, message, sizeof(message) - 1);
        if (received > 0) {
            message[received] = '\0';
            for (int i = 0; i <= received; i++) filename[i] = message[i];
            break;
        }
        api->idle();
    }

    size_t loaded = 0;
    if (api->fs_load(filename, text, BUFFER_MAX, &loaded)) {
        length = (int)loaded;
    }
    text[length] = '\0';
    cursor = length;
    redraw(api, "READY", UI_TEXT);

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) return 0;
        if (key < 0) {
            ui_flush(&ui);
            continue;
        }
        if (key == 0x11 || key == 0x1b) { /* Ctrl-Q or Esc */
            return 0;
        }
        if (key == 0x13) { /* Ctrl-S */
            if (api->fs_save(filename, text, (size_t)length)) {
                api->puts("SAVED ");
                api->puts(filename);
                api->putc('\n');
                redraw(api, "SAVED", UI_GOOD);
            } else {
                redraw(api, "SAVE FAILED", UI_WARN);
            }
            ui_flush(&ui);
            continue;
        }
        if (key == C26_KEY_UP || key == 0x10) move_line(-1);
        else if (key == C26_KEY_DOWN || key == 0x0e) move_line(1);
        else if ((key == C26_KEY_LEFT || key == 0x02) && cursor > 0) cursor--;
        else if ((key == C26_KEY_RIGHT || key == 0x06) && cursor < length)
            cursor++;
        else if (key == '\b' || key == 0x7f) erase();
        else if (key == '\n') insert('\n');
        else if (key >= 32 && key <= 126) insert((char)key);
        redraw(api, "EDITING", UI_TEXT);
        ui_flush(&ui);
    }
}
