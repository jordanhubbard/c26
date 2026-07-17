#ifndef C26_UI_H
#define C26_UI_H

/* c26_ui: a small immediate-mode toolkit for windowed cartridges. Linked
 * into each app; talks to the machine only through the c26_api_t. Call
 * ui_init once, then each loop: ui_poll (input + housekeeping), draw with
 * the helpers, ui_flush (throttled present), api->idle via ui_poll. */

#include "c26_api.h"

#define UI_BG 0x0b1025U
#define UI_PANEL 0x222957U
#define UI_ACCENT 0x35409aU
#define UI_TEXT 0xbac4ffU
#define UI_BRIGHT 0xffffffU
#define UI_GOOD 0x68f0c0U
#define UI_WARN 0xffd34dU

typedef struct {
    const c26_api_t *api;
    uint32_t width;
    uint32_t height;
    int mouse_x;
    int mouse_y;
    int mouse_buttons;
    int clicked; /* left button pressed this poll (edge) */
    int dirty;
    uint64_t last_present;
} c26_ui_t;

void ui_init(c26_ui_t *ui, const c26_api_t *api);

/* Pumps input and idles; returns a key code, or -1 for none, or -2 when
 * the kernel asked the app to stop. */
int ui_poll(c26_ui_t *ui);

void ui_flush(c26_ui_t *ui);

void ui_clear(c26_ui_t *ui);
void ui_titlebar(c26_ui_t *ui, const char *title, const char *hint);
void ui_text(c26_ui_t *ui, int x, int y, const char *text, uint32_t color);
void ui_status(c26_ui_t *ui, const char *text, uint32_t color);

/* A selectable list row; returns 1 when the row was clicked. */
int ui_row(c26_ui_t *ui, int y, const char *text, int selected);

int ui_hit(const c26_ui_t *ui, int x, int y, int w, int h);

#endif
