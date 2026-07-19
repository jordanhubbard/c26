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

static void draw(const c26_api_t *api)
{
    api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
    api->fill_rect(0, 0, (int)width, TITLE_H, 0x222957);
    api->text(6, 4, "SHEET", 0xffffff, 0x222957, 2);

    char buf[24];
    int x, y, w, h;

    /* Header row: corner, column labels A..E, then SUM. */
    for (int gc = 0; gc < COLS + 2; gc++) {
        cell_rect(gc, 0, &x, &y, &w, &h);
        api->fill_rect(x, y, w, h, 0x2b3466);
        if (gc >= 1 && gc <= COLS) {
            char lbl[2] = {(char)('A' + gc - 1), '\0'};
            api->text(x + w / 2 - 3, y + h / 2 - 4, lbl, 0xffd34d, 0x2b3466, 1);
        } else if (gc == COLS + 1) {
            api->text(x + w / 2 - 9, y + h / 2 - 4, "SUM", 0xffd34d,
                      0x2b3466, 1);
        }
    }

    /* Data rows: row label, cells, row sum. */
    for (int r = 0; r < ROWS; r++) {
        int gr = r + 1;
        cell_rect(0, gr, &x, &y, &w, &h);
        api->fill_rect(x, y, w, h, 0x2b3466);
        format_int(r + 1, buf);
        api->text(x + w / 2 - 3, y + h / 2 - 4, buf, 0xffd34d, 0x2b3466, 1);

        for (int c = 0; c < COLS; c++) {
            cell_rect(c + 1, gr, &x, &y, &w, &h);
            int selected = (r == cur_r && c == cur_c);
            uint32_t bg = selected ? 0x35509a : 0x141b3a;
            api->fill_rect(x, y, w, h, bg);
            api->draw_rect(x, y, w, h, 0x2b3466);
            if (selected && editing) {
                format_int(edit_neg ? -edit_val : edit_val, buf);
            } else {
                format_int(cell[r][c], buf);
            }
            api->text(x + 3, y + h / 2 - 4, buf, 0xffffff, bg, 1);
        }

        cell_rect(COLS + 1, gr, &x, &y, &w, &h);
        api->fill_rect(x, y, w, h, 0x1c2a4a);
        format_int(row_sum(r), buf);
        api->text(x + 3, y + h / 2 - 4, buf, 0x68f0c0, 0x1c2a4a, 1);
    }

    /* SUM row: label, per-column sums, grand total in the corner. */
    int sgr = ROWS + 1;
    cell_rect(0, sgr, &x, &y, &w, &h);
    api->fill_rect(x, y, w, h, 0x2b3466);
    api->text(x + w / 2 - 9, y + h / 2 - 4, "SUM", 0xffd34d, 0x2b3466, 1);
    for (int c = 0; c < COLS; c++) {
        cell_rect(c + 1, sgr, &x, &y, &w, &h);
        api->fill_rect(x, y, w, h, 0x1c2a4a);
        format_int(col_sum(c), buf);
        api->text(x + 3, y + h / 2 - 4, buf, 0x68f0c0, 0x1c2a4a, 1);
    }
    cell_rect(COLS + 1, sgr, &x, &y, &w, &h);
    api->fill_rect(x, y, w, h, 0x2f7a4a);
    format_int(grand_total, buf);
    api->text(x + 3, y + h / 2 - 4, buf, 0xffffff, 0x2f7a4a, 1);
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
