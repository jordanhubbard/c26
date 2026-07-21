/* SHEET: a tiny integer spreadsheet. A ROWS x COLS grid of cells (columns
 * A..E, rows 1..6). Arrow keys move the cursor; digits build a number for the
 * selected cell, Enter commits it, Backspace deletes a digit, '-' negates, and
 * 'C' clears the cell. Auto-computed SUM row, SUM column, and grand total are
 * recomputed on every edit. Talks to the machine only through the c26_api_t.
 * Each commit (and any change to the grand total) prints SHEET TOTAL <n> so
 * the total is checkable over the serial line. */

#include "c26_api.h"

#define COLS 5
#define ROWS 6
#define TITLE_H 24
#define GRID_TOP 30

static uint32_t width;
static uint32_t height;

static int64_t cell[ROWS][COLS]; /* the sheet, zero-initialized in .bss */

static int cur_r;      /* cursor row (0..ROWS-1) */
static int cur_c;      /* cursor column (0..COLS-1) */

static int editing;    /* a number is being typed into the current cell */
static int64_t edit_val; /* magnitude of the number being typed */
static int edit_neg;   /* the entry has a leading minus */

static int64_t grand_total; /* last reported sum of every cell */

/* Render an int64 as a decimal string (no libc). */
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

static int64_t col_sum(int c)
{
    int64_t s = 0;
    for (int r = 0; r < ROWS; r++) s += cell[r][c];
    return s;
}

static int64_t row_sum(int r)
{
    int64_t s = 0;
    for (int c = 0; c < COLS; c++) s += cell[r][c];
    return s;
}

static int64_t compute_total(void)
{
    int64_t s = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) s += cell[r][c];
    return s;
}

/* Print SHEET TOTAL when the grand total changed, or unconditionally when a
 * commit forces it. Keeps grand_total in sync for the display. */
static void refresh_total(const c26_api_t *api, int force)
{
    int64_t t = compute_total();
    if (force || t != grand_total) {
        grand_total = t;
        api->puts("SHEET TOTAL ");
        api->put_int(grand_total);
        api->putc('\n');
    }
}

/* Fold any in-progress entry into the current cell. */
static void write_edit(void)
{
    if (editing) {
        cell[cur_r][cur_c] = edit_neg ? -edit_val : edit_val;
        editing = 0;
        edit_val = 0;
        edit_neg = 0;
    }
}

/* Geometry: the grid has COLS+2 columns (row label, data, SUM) and ROWS+2
 * rows (header, data, SUM). gc/gr are grid-relative indices. */
static void cell_rect(int gc, int gr, int *x, int *y, int *w, int *h)
{
    int cw = (int)width / (COLS + 2);
    int chh = ((int)height - GRID_TOP) / (ROWS + 2);
    *x = gc * cw;
    *y = GRID_TOP + gr * chh;
    *w = cw - 1;
    *h = chh - 1;
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
    /* desktop palette */
    const uint32_t grid = cmix(0xc3ccf5, 0x0b1025, 200); /* dim grid lines */

    vgrad(api, 0, 0, (int)width, (int)height, 0x141a36, 0x0b1025);
    /* title bar: panel gradient with bright top / dark seam bevel */
    vgrad(api, 0, 0, (int)width, TITLE_H, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, (int)width, 1, cmix(0x30397e, 0xffffff, 90));
    api->fill_rect(0, TITLE_H - 1, (int)width, 1, cmix(0x191f48, 0x000000, 90));
    label_fg(api, 6, 4, "SHEET", 0xffffff, 0x30397e, 2);

    char buf[24];
    int x, y, w, h;

    /* A glossy accent-blue header cell with a bevel (bright top, dark seam). */
    #define HEADER_CELL()                                                     \
        do {                                                                  \
            vgrad(api, x, y, w, h, 0x4653b4, 0x2f3894);                        \
            api->fill_rect(x, y, w, 1, cmix(0x4653b4, 0xffffff, 90));          \
            api->fill_rect(x, y + h - 1, w, 1, cmix(0x2f3894, 0x000000, 90));  \
        } while (0)

    /* Header row: corner, column labels A..E, then SUM. */
    for (int gc = 0; gc < COLS + 2; gc++) {
        cell_rect(gc, 0, &x, &y, &w, &h);
        HEADER_CELL();
        if (gc >= 1 && gc <= COLS) {
            char lbl[2] = {(char)('A' + gc - 1), '\0'};
            label_fg(api, x + w / 2 - 3, y + h / 2 - 4, lbl, 0xffd34d, 0x4653b4, 1);
        } else if (gc == COLS + 1) {
            label_fg(api, x + w / 2 - 9, y + h / 2 - 4, "SUM", 0xffd34d,
                     0x4653b4, 1);
        }
    }

    /* Data rows: row label, cells, row sum. */
    for (int r = 0; r < ROWS; r++) {
        int gr = r + 1;
        cell_rect(0, gr, &x, &y, &w, &h);
        HEADER_CELL();
        format_int(r + 1, buf);
        label_fg(api, x + w / 2 - 3, y + h / 2 - 4, buf, 0xffd34d, 0x4653b4, 1);

        for (int c = 0; c < COLS; c++) {
            cell_rect(c + 1, gr, &x, &y, &w, &h);
            int selected = (r == cur_r && c == cur_c);
            if (selected) {
                /* selected cell: glossy accent highlight with a top light line */
                vgrad(api, x, y, w, h, cmix(0x4653b4, 0xffffff, 40), 0x2f3894);
                api->fill_rect(x, y, w, 1, cmix(0x4653b4, 0xffffff, 130));
            } else {
                vgrad(api, x, y, w, h, 0x141a36, 0x0f1430);
            }
            api->draw_rect(x, y, w, h, grid);
            if (selected && editing) {
                format_int(edit_neg ? -edit_val : edit_val, buf);
            } else {
                format_int(cell[r][c], buf);
            }
            label_fg(api, x + 3, y + h / 2 - 4, buf, 0xffffff, 0x141a36, 1);
        }

        cell_rect(COLS + 1, gr, &x, &y, &w, &h);
        vgrad(api, x, y, w, h, 0x24325a, 0x141f3c);
        api->fill_rect(x, y, w, 1, cmix(0x24325a, 0xffffff, 60));
        format_int(row_sum(r), buf);
        label_fg(api, x + 3, y + h / 2 - 4, buf, 0x68f0c0, 0x24325a, 1);
    }

    /* SUM row: label, per-column sums, grand total in the corner. */
    int sgr = ROWS + 1;
    cell_rect(0, sgr, &x, &y, &w, &h);
    HEADER_CELL();
    label_fg(api, x + w / 2 - 9, y + h / 2 - 4, "SUM", 0xffd34d, 0x4653b4, 1);
    for (int c = 0; c < COLS; c++) {
        cell_rect(c + 1, sgr, &x, &y, &w, &h);
        vgrad(api, x, y, w, h, 0x24325a, 0x141f3c);
        api->fill_rect(x, y, w, 1, cmix(0x24325a, 0xffffff, 60));
        format_int(col_sum(c), buf);
        label_fg(api, x + 3, y + h / 2 - 4, buf, 0x68f0c0, 0x24325a, 1);
    }
    cell_rect(COLS + 1, sgr, &x, &y, &w, &h);
    vgrad(api, x, y, w, h, 0x2f9a54, 0x1c5a34);
    api->fill_rect(x, y, w, 1, cmix(0x2f9a54, 0xffffff, 90));
    api->fill_rect(x, y + h - 1, w, 1, cmix(0x1c5a34, 0x000000, 90));
    format_int(grand_total, buf);
    label_fg(api, x + 3, y + h / 2 - 4, buf, 0xffffff, 0x2f9a54, 1);

    #undef HEADER_CELL
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("SHEET CART ONLINE\n");
    draw(api);
    api->present();

    int dirty = 0;
    uint64_t last_present = 0;
    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;

        if (ch == C26_KEY_UP) {
            write_edit();
            refresh_total(api, 0);
            if (cur_r > 0) cur_r--;
            dirty = 1;
        } else if (ch == C26_KEY_DOWN) {
            write_edit();
            refresh_total(api, 0);
            if (cur_r < ROWS - 1) cur_r++;
            dirty = 1;
        } else if (ch == C26_KEY_LEFT) {
            write_edit();
            refresh_total(api, 0);
            if (cur_c > 0) cur_c--;
            dirty = 1;
        } else if (ch == C26_KEY_RIGHT) {
            write_edit();
            refresh_total(api, 0);
            if (cur_c < COLS - 1) cur_c++;
            dirty = 1;
        } else if (ch == '\n' || ch == '\r') {
            write_edit();
            refresh_total(api, 1); /* Enter always reports the total */
            dirty = 1;
        } else if (ch == '\b' || ch == 0x7f) {
            if (editing) edit_val /= 10;
            dirty = 1;
        } else if (ch == '-') {
            if (!editing) { editing = 1; edit_val = 0; }
            edit_neg = !edit_neg;
            dirty = 1;
        } else if (ch >= '0' && ch <= '9') {
            if (!editing) { editing = 1; edit_val = 0; edit_neg = 0; }
            edit_val = edit_val * 10 + (ch - '0');
            dirty = 1;
        } else if (ch == 'C' || ch == 'c') {
            cell[cur_r][cur_c] = 0;
            editing = 0;
            edit_val = 0;
            edit_neg = 0;
            refresh_total(api, 0);
            dirty = 1;
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
    api->puts("SHEET CART EXIT\n");
    return 0;
}
