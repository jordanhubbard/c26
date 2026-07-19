/* CALC: a four-function calculator. A mouse-clickable keypad plus keyboard
 * (digits, + - * /, = or Enter, C clears, Q/Esc quits). Arithmetic is
 * left-to-right like a pocket calculator. Talks to the machine only through
 * the c26_api_t. Each '=' prints CALC = <n> so the result is checkable. */

#include "c26_api.h"

#define PAD_TOP 56
#define COLS 4
#define ROWS 4

static uint32_t width;
static uint32_t height;

static int64_t acc;    /* accumulator */
static int64_t cur;    /* number being entered */
static char op;        /* pending operator, 0 = none */
static int entering;   /* digits have been typed into cur */
static int error;      /* division by zero */

static const char keys[ROWS][COLS] = {
    {'7', '8', '9', '/'},
    {'4', '5', '6', '*'},
    {'1', '2', '3', '-'},
    {'0', 'C', '=', '+'},
};

static void format_int(int64_t value, char *out)
{
    char tmp[24];
    int n = 0;
    int negative = value < 0;
    uint64_t u = negative ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    if (u == 0) tmp[n++] = '0';
    while (u != 0) {
        tmp[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    }
    int k = 0;
    if (negative) out[k++] = '-';
    while (n > 0) out[k++] = tmp[--n];
    out[k] = '\0';
}

static void apply(void)
{
    if (op == 0) {
        acc = cur;
    } else if (op == '+') {
        acc += cur;
    } else if (op == '-') {
        acc -= cur;
    } else if (op == '*') {
        acc *= cur;
    } else if (op == '/') {
        if (cur == 0) { error = 1; return; }
        acc /= cur;
    }
    cur = 0;
}

/* Layout: each key occupies a cell of the keypad grid below the display. */
static void key_rect(int row, int col, int *x, int *y, int *w, int *h)
{
    int cw = (int)width / COLS;
    int ch = ((int)height - PAD_TOP) / ROWS;
    *x = col * cw;
    *y = PAD_TOP + row * ch;
    *w = cw - 2;
    *h = ch - 2;
}

static void draw(const c26_api_t *api)
{
    api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
    api->fill_rect(0, 0, (int)width, 24, 0x222957);
    api->text(6, 4, "CALC", 0xffffff, 0x222957, 2);

    /* Display bar shows the entry, or the accumulator, or ERROR. */
    api->fill_rect(4, 26, (int)width - 8, 26, 0x1a2140);
    char buf[24];
    if (error) {
        api->text(10, 32, "ERROR", 0xffd34d, 0x1a2140, 2);
    } else {
        format_int(entering ? cur : acc, buf);
        api->text(10, 32, buf, 0x68f0c0, 0x1a2140, 2);
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int x, y, w, h;
            key_rect(r, c, &x, &y, &w, &h);
            char k = keys[r][c];
            uint32_t bg = (k >= '0' && k <= '9') ? 0x35409a : 0x5a3a7a;
            if (k == '=') bg = 0x2f7a4a;
            if (k == 'C') bg = 0x8a3030;
            api->fill_rect(x, y, w, h, bg);
            char label[2] = {k, '\0'};
            api->text(x + w / 2 - 5, y + h / 2 - 7, label, 0xffffff, bg, 2);
        }
    }
}

static void press(const c26_api_t *api, char k)
{
    if (error && k != 'C') return;
    if (k >= '0' && k <= '9') {
        cur = cur * 10 + (k - '0');
        entering = 1;
    } else if (k == 'C') {
        acc = 0;
        cur = 0;
        op = 0;
        entering = 0;
        error = 0;
    } else if (k == '=') {
        apply();
        if (!error) {
            char buf[24];
            format_int(acc, buf);
            api->puts("CALC = ");
            api->puts(buf);
            api->putc('\n');
        }
        op = 0;
        entering = 0;
    } else { /* + - * / */
        /* Only fold in a freshly-typed operand; chaining straight after '='
           (or a repeated operator) keeps the current result as the left side. */
        if (entering) apply();
        op = k;
        entering = 0;
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("CALC CART ONLINE\n");
    draw(api);
    api->present();

    int last_buttons = 0;
    int dirty = 0;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;
        if (ch == '\n' || ch == '\r') { press(api, '='); dirty = 1; }
        else if (ch == 'c') { press(api, 'C'); dirty = 1; }
        else if (ch > 0 && ((ch >= '0' && ch <= '9') || ch == '+' ||
                            ch == '-' || ch == '*' || ch == '/' ||
                            ch == '=' || ch == 'C')) {
            press(api, (char)ch);
            dirty = 1;
        }

        int x, y, buttons;
        api->mouse(&x, &y, &buttons);
        if ((buttons & 1) != 0 && last_buttons == 0) {
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < COLS; c++) {
                    int kx, ky, kw, kh;
                    key_rect(r, c, &kx, &ky, &kw, &kh);
                    if (x >= kx && x < kx + kw && y >= ky && y < ky + kh) {
                        press(api, keys[r][c]);
                        dirty = 1;
                    }
                }
            }
        }
        last_buttons = buttons & 1;

        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 2) {
            last_present = now;
            dirty = 0;
            draw(api);
            api->present();
        }
        api->idle();
    }
    api->puts("CALC CART EXIT\n");
    return 0;
}
