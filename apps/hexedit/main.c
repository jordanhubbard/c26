/* HEXEDIT: a classic hex editor for C26FS files. Loads a file (NOTES by
 * default, or a name handed over IPC at startup like EDIT) into a byte
 * buffer and shows it as offset + 16 hex bytes + an ASCII gutter per row.
 * Arrow keys move the byte cursor; typing a hex digit (0-9 A-F) edits the
 * high then low nibble of the selected byte; Ctrl-S saves; Q or Esc quits.
 * Talks to the machine only through the c26_api_t. */

#include "c26_api.h"

#define BUFFER_MAX 8192
#define BYTES_PER_ROW 16
#define TOP 32           /* first pixel row of the hex dump */
#define ROW_HEIGHT 18
#define LEFT 8

static uint32_t width;
static uint32_t height;

static uint8_t data[BUFFER_MAX];
static int size;             /* bytes currently in the buffer */
static int cursor;           /* selected byte index */
static int high_nibble;      /* 1 = next hex digit edits the high nibble */
static char filename[16] = "NOTES";

/* One hex character for a value 0..15. */
static char hex_digit(int value)
{
    return (value < 10) ? (char)('0' + value) : (char)('A' + (value - 10));
}

/* Write the two hex characters of a byte into out (not NUL-terminated). */
static void byte_to_hex(uint8_t byte, char *out)
{
    out[0] = hex_digit((byte >> 4) & 0xf);
    out[1] = hex_digit(byte & 0xf);
}

/* Value 0..15 for a hex key, or -1 if the key is not a hex digit. */
static int hex_value(int key)
{
    if (key >= '0' && key <= '9') return key - '0';
    if (key >= 'A' && key <= 'F') return key - 'A' + 10;
    if (key >= 'a' && key <= 'f') return key - 'a' + 10;
    return -1;
}

/* How many dump rows fit in the window. */
static int visible_rows(void)
{
    int rows = ((int)height - TOP - 4) / ROW_HEIGHT;
    return rows < 1 ? 1 : rows;
}

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

static void draw(const c26_api_t *api)
{
    vgrad(api, 0, 0, (int)width, (int)height, 0x141a36, 0x0b1025);
    /* title bar: glossy panel gradient, bright top highlight and dark seam */
    vgrad(api, 0, 0, (int)width, 24, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, 0x4a56b4);
    api->fill_rect(0, 24, (int)width, 1, 0x090d1e);
    label_fg(api, 6, 4, "HEXEDIT", 0xffffff, 0x30397e, 2);

    int rows = visible_rows();
    /* Scroll so the cursor's row stays on screen. */
    int cursor_row = (size == 0) ? 0 : cursor / BYTES_PER_ROW;
    int top_row = 0;
    if (cursor_row >= rows) top_row = cursor_row - rows + 1;

    for (int r = 0; r < rows; r++) {
        int row = top_row + r;
        int base = row * BYTES_PER_ROW;
        if (base >= size && !(size == 0 && row == 0)) break;
        int y = TOP + r * ROW_HEIGHT;

        /* Build one line: "OFFSET  HH HH ...  |ascii|". */
        char line[80];
        int n = 0;

        /* 4-digit hex offset. */
        line[n++] = hex_digit((base >> 12) & 0xf);
        line[n++] = hex_digit((base >> 8) & 0xf);
        line[n++] = hex_digit((base >> 4) & 0xf);
        line[n++] = hex_digit(base & 0xf);
        line[n++] = ' ';
        line[n++] = ' ';

        /* Hex bytes. */
        for (int c = 0; c < BYTES_PER_ROW; c++) {
            int index = base + c;
            if (index < size) {
                byte_to_hex(data[index], &line[n]);
                n += 2;
            } else {
                line[n++] = ' ';
                line[n++] = ' ';
            }
            line[n++] = ' ';
        }

        /* ASCII gutter. */
        line[n++] = '|';
        for (int c = 0; c < BYTES_PER_ROW; c++) {
            int index = base + c;
            if (index < size) {
                uint8_t b = data[index];
                line[n++] = (b >= 32 && b <= 126) ? (char)b : '.';
            } else {
                line[n++] = ' ';
            }
        }
        line[n++] = '|';
        line[n] = '\0';

        api->text(LEFT, y, line, 0x68f0c0, 0x0b1025, 1);

        /* Highlight the selected byte on its row with a glossy accent cell. */
        if (size > 0 && cursor >= base && cursor < base + BYTES_PER_ROW) {
            int col = cursor - base;
            /* 6 leading chars (offset + two spaces), then 3 chars per byte,
               each character 6 pixels wide at scale 1. */
            int hx = LEFT + (6 + col * 3) * 6;
            /* accent-blue gradient cell + a 1px bright top highlight line */
            vgrad(api, hx - 1, y - 1, 14, 13, 0x4653b4, 0x2f3894);
            api->fill_rect(hx - 1, y - 1, 14, 1, cmix(0x4653b4, 0xffffff, 90));
            char cell[3];
            byte_to_hex(data[cursor], cell);
            cell[2] = '\0';
            label_fg(api, hx, y, cell, 0xffffff, 0x4653b4, 1);
        }
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("HEXEDIT CART ONLINE\n");

    /* A launcher may hand us a filename over IPC (as EDIT does). */
    uint64_t deadline = api->ticks() + 50;
    while (api->ticks() < deadline) {
        char message[16];
        int received = api->recv(0, message, sizeof(message) - 1);
        if (received > 0) {
            if (received > 15) received = 15;
            for (int i = 0; i < received; i++) filename[i] = message[i];
            filename[received] = '\0';
            break;
        }
        api->idle();
    }

    /* Load the file; an empty or missing file just starts empty. */
    size_t loaded = 0;
    if (api->fs_load(filename, data, BUFFER_MAX, &loaded)) {
        size = (int)loaded;
    } else {
        size = 0;
    }
    cursor = 0;
    high_nibble = 1;

    api->puts("HEXEDIT ");
    api->puts(filename);
    api->putc(' ');
    api->put_int((int64_t)size);
    api->putc('\n');

    draw(api);
    api->present();

    int dirty = 0;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 'q' || ch == 0x1b) break;

        if (ch == C26_KEY_LEFT) {
            if (cursor > 0) cursor--;
            high_nibble = 1;
            dirty = 1;
        } else if (ch == C26_KEY_RIGHT) {
            if (cursor + 1 < size) cursor++;
            high_nibble = 1;
            dirty = 1;
        } else if (ch == C26_KEY_UP) {
            if (cursor >= BYTES_PER_ROW) cursor -= BYTES_PER_ROW;
            high_nibble = 1;
            dirty = 1;
        } else if (ch == C26_KEY_DOWN) {
            if (cursor + BYTES_PER_ROW < size) cursor += BYTES_PER_ROW;
            high_nibble = 1;
            dirty = 1;
        } else if (ch == 0x13) { /* Ctrl-S: save */
            if (api->fs_save(filename, data, (size_t)size)) {
                api->puts("HEXEDIT SAVED\n");
            }
            dirty = 1;
        } else {
            int value = hex_value(ch);
            if (value >= 0 && size > 0) {
                /* Edit the high then the low nibble of the selected byte. */
                if (high_nibble) {
                    data[cursor] = (uint8_t)((data[cursor] & 0x0f) |
                                             (value << 4));
                    high_nibble = 0;
                } else {
                    data[cursor] = (uint8_t)((data[cursor] & 0xf0) | value);
                    high_nibble = 1;
                    if (cursor + 1 < size) cursor++;
                }
                dirty = 1;
            }
        }

        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 2) {
            last_present = now;
            dirty = 0;
            draw(api);
            api->present();
        }
        api->idle();
    }

    api->puts("HEXEDIT CART EXIT\n");
    return 0;
}
