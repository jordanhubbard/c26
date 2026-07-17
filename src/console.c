#include "c26.h"
#include "c26_console.h"
#include "c26_graphics.h"

#define CONSOLE_ORIGIN_X 20
#define CONSOLE_ORIGIN_Y 15
#define CONSOLE_CELL_WIDTH 6
#define CONSOLE_CELL_HEIGHT 10
#define CONSOLE_FG 0xbac4ffU
#define CONSOLE_BG 0x0b1025U

static char cells[C26_CONSOLE_ROWS][C26_CONSOLE_COLS];
static unsigned int cursor_row;
static unsigned int cursor_col;
static int dirty;
static c26_screen_mode_t screen_mode = C26_SCREEN_CONSOLE;

void c26_console_clear(void)
{
    for (unsigned int row = 0; row < C26_CONSOLE_ROWS; row++) {
        for (unsigned int col = 0; col < C26_CONSOLE_COLS; col++) {
            cells[row][col] = ' ';
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    dirty = 1;
}

static void scroll_up(void)
{
    for (unsigned int row = 1; row < C26_CONSOLE_ROWS; row++) {
        memcpy(cells[row - 1], cells[row], C26_CONSOLE_COLS);
    }
    for (unsigned int col = 0; col < C26_CONSOLE_COLS; col++) {
        cells[C26_CONSOLE_ROWS - 1][col] = ' ';
    }
}

void c26_console_putc(char ch)
{
    if (ch == '\r') {
        return;
    }
    if (ch == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (ch == '\b') {
        if (cursor_col != 0) {
            cursor_col--;
            cells[cursor_row][cursor_col] = ' ';
        }
    } else if (ch >= ' ' && ch <= '~') {
        cells[cursor_row][cursor_col] = ch;
        cursor_col++;
        if (cursor_col == C26_CONSOLE_COLS) {
            cursor_col = 0;
            cursor_row++;
        }
    } else {
        return;
    }
    if (cursor_row == C26_CONSOLE_ROWS) {
        scroll_up();
        cursor_row = C26_CONSOLE_ROWS - 1;
    }
    dirty = 1;
}

void c26_console_render_cells(void)
{
    c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, (int)C26_SCREEN_HEIGHT,
                  CONSOLE_BG);
    for (unsigned int row = 0; row < C26_CONSOLE_ROWS; row++) {
        for (unsigned int col = 0; col < C26_CONSOLE_COLS; col++) {
            if (cells[row][col] != ' ') {
                c26_draw_char(CONSOLE_ORIGIN_X + (int)col * CONSOLE_CELL_WIDTH,
                              CONSOLE_ORIGIN_Y + (int)row * CONSOLE_CELL_HEIGHT,
                              cells[row][col], CONSOLE_FG, CONSOLE_BG, 1);
            }
        }
    }
    c26_fill_rect(CONSOLE_ORIGIN_X + (int)cursor_col * CONSOLE_CELL_WIDTH,
                  CONSOLE_ORIGIN_Y + (int)cursor_row * CONSOLE_CELL_HEIGHT,
                  CONSOLE_CELL_WIDTH - 1, CONSOLE_CELL_HEIGHT - 2, CONSOLE_FG);
    dirty = 0;
}

void c26_console_render(void)
{
    c26_console_render_cells();
    c26_framebuffer_present();
}

int c26_console_dirty(void)
{
    return dirty;
}

void c26_console_flush(void)
{
    if (dirty && screen_mode == C26_SCREEN_CONSOLE) {
        c26_console_render();
    }
}

c26_screen_mode_t c26_screen_mode(void)
{
    return screen_mode;
}

void c26_screen_set_mode(c26_screen_mode_t mode)
{
    if (mode == C26_SCREEN_CONSOLE && screen_mode != C26_SCREEN_CONSOLE) {
        dirty = 1;
    }
    screen_mode = mode;
}

void c26_putc(char ch)
{
    c26_uart_putc(ch);
    c26_console_putc(ch);
}

void c26_puts(const char *text)
{
    while (text != 0 && *text != '\0') {
        c26_putc(*text++);
    }
}

void c26_put_uint(uint64_t value)
{
    char digits[21];
    size_t used = 0;
    if (value == 0) {
        c26_putc('0');
        return;
    }
    while (value != 0 && used < sizeof(digits)) {
        digits[used++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (used != 0) {
        c26_putc(digits[--used]);
    }
}

void c26_put_int(int64_t value)
{
    if (value < 0) {
        c26_putc('-');
        c26_put_uint((uint64_t)-value);
        return;
    }
    c26_put_uint((uint64_t)value);
}

void c26_put_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    c26_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        c26_putc(hex[(value >> shift) & 0xf]);
    }
}
