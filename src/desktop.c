#include "c26.h"
#include "c26_console.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_input.h"
#include "c26_net.h"

#define KEY_ESC 1U
#define KEY_BACKSPACE 14U
#define KEY_TAB 15U
#define KEY_ENTER 28U
#define KEY_LEFTSHIFT 42U
#define KEY_RIGHTSHIFT 54U
#define KEY_UP 103U
#define KEY_LEFT 105U
#define KEY_RIGHT 106U
#define KEY_DOWN 108U
#define BTN_LEFT 272U

static int pointer_x = 310;
static int pointer_y = 230;
static int pointer_buttons;
static int shift_down;

static void draw_pointer(void)
{
    c26_draw_line(pointer_x, pointer_y, pointer_x, pointer_y + 14, 0xffffff);
    c26_draw_line(pointer_x, pointer_y, pointer_x + 9, pointer_y + 9, 0xffffff);
    c26_draw_line(pointer_x, pointer_y + 14, pointer_x + 4, pointer_y + 10, 0xffffff);
}

void c26_desktop_draw_pointer(void)
{
    draw_pointer();
}

void c26_desktop_init(void)
{
    c26_framebuffer_init();
    c26_input_init();
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    c26_puts("C26 DESKTOP: graphical shell online\n");
    c26_puts("DESKTOP INPUT: click a window to focus, drag its title to move\n");
}

static void move_pointer(c26_input_event_t event, int minimum_y)
{
    if (event.code == 0) pointer_x += event.value;
    if (event.code == 1) pointer_y += event.value;
    if (pointer_x < 0) pointer_x = 0;
    if (pointer_y < minimum_y) pointer_y = minimum_y;
    if (pointer_x >= (int)C26_SCREEN_WIDTH) pointer_x = C26_SCREEN_WIDTH - 1;
    if (pointer_y >= (int)C26_SCREEN_HEIGHT) pointer_y = C26_SCREEN_HEIGHT - 1;
}

static void cart_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_RELATIVE) {
        move_pointer(event, 0);
        c26_wm_pointer_moved(pointer_x, pointer_y);
        return;
    }
    if (event.type != C26_INPUT_EVENT_KEY) {
        return;
    }
    if (event.code == BTN_LEFT) {
        c26_wm_click(pointer_x, pointer_y, event.value != 0);
        return;
    }
    if (event.value == 0) {
        return;
    }
    if (event.code == KEY_TAB) {
        c26_cart_focus_next();
        return;
    }
    if (event.code == KEY_ESC) {
        c26_basic_feed_char(0x1b);
        return;
    }
    if (event.code == KEY_ENTER) {
        c26_basic_feed_char('\n');
        return;
    }
    if (event.code == KEY_BACKSPACE) {
        c26_basic_feed_char('\b');
        return;
    }
    /* Arrow keys reach apps as the C26_KEY_* codes from c26_api.h. */
    if (event.code == KEY_UP) {
        c26_basic_feed_char(0x1c);
        return;
    }
    if (event.code == KEY_DOWN) {
        c26_basic_feed_char(0x1d);
        return;
    }
    if (event.code == KEY_RIGHT) {
        c26_basic_feed_char(0x1e);
        return;
    }
    if (event.code == KEY_LEFT) {
        c26_basic_feed_char(0x1f);
        return;
    }
    char ch = c26_input_key_to_ascii(event.code, shift_down);
    if (ch != 0) {
        c26_basic_feed_char(ch);
    }
}

static void console_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_RELATIVE) {
        move_pointer(event, 0);
        c26_wm_pointer_moved(pointer_x, pointer_y);
        return;
    }
    if (event.type != C26_INPUT_EVENT_KEY) {
        return;
    }
    if (event.code == BTN_LEFT) {
        c26_wm_click(pointer_x, pointer_y, event.value != 0);
        return;
    }
    if (event.value == 0) {
        return;
    }
    if (event.code == KEY_TAB) {
        c26_cart_focus_next();
        return;
    }
    if (event.code == KEY_ESC) {
        /* Break a running program; otherwise it's a no-op on the unified
           desktop (the dock, not a separate menu, launches apps). */
        if (c26_basic_running()) {
            c26_basic_feed_char(0x1b);
        }
        return;
    }
    if (event.code == KEY_ENTER) {
        c26_basic_feed_char('\n');
        return;
    }
    if (event.code == KEY_BACKSPACE) {
        c26_basic_feed_char('\b');
        return;
    }
    char ch = c26_input_key_to_ascii(event.code, shift_down);
    if (ch != 0) {
        c26_basic_feed_char(ch);
    }
}

static void handle_input_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_KEY &&
        (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)) {
        shift_down = event.value != 0;
        return;
    }
    if (event.type == C26_INPUT_EVENT_KEY && event.code == BTN_LEFT) {
        if (event.value != 0) pointer_buttons |= 1;
        else pointer_buttons &= ~1;
    }
    switch (c26_screen_mode()) {
    case C26_SCREEN_CART:
        cart_event(event);
        break;
    case C26_SCREEN_CONSOLE:
    case C26_SCREEN_GFX:
    case C26_SCREEN_DESKTOP: /* retired; treat as the console desktop */
        console_event(event);
        break;
    }
}

void c26_desktop_mouse(int *x, int *y, int *buttons)
{
    if (x != 0) *x = pointer_x;
    if (y != 0) *y = pointer_y;
    if (buttons != 0) *buttons = pointer_buttons;
}

/* Synthetic pointer input: move the pointer to an absolute position and route
   it exactly as a real virtio-mouse move would. Lets BASIC (and the smoke
   gate) drive the window manager and dock through the actual hit-test path. */
void c26_desktop_inject_pointer(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= (int)C26_SCREEN_WIDTH) x = (int)C26_SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= (int)C26_SCREEN_HEIGHT) y = (int)C26_SCREEN_HEIGHT - 1;
    pointer_x = x;
    pointer_y = y;
    c26_wm_pointer_moved(pointer_x, pointer_y);
}

/* Synthetic left-button press (1) or release (0) at the current pointer. */
void c26_desktop_inject_button(int pressed)
{
    c26_input_event_t event = {C26_INPUT_EVENT_KEY, BTN_LEFT,
                               pressed ? 1 : 0};
    handle_input_event(event);
    c26_compositor_mark_dirty();
}

void c26_io_pump(void)
{
    int ch;
    while (c26_basic_can_accept() &&
           (ch = c26_uart_getc_nonblocking()) >= 0) {
        c26_basic_feed_char((char)ch);
    }
    c26_input_event_t event;
    while (c26_input_poll(&event)) {
        handle_input_event(event);
    }
    c26_audio_poll();
    c26_net_poll();
}

void c26_desktop_poll(void)
{
    static uint64_t last_flush_tick;
    c26_io_pump();
    /* Presenting is a full-screen GPU transfer; cap it at one render per
       5 timer ticks so typing bursts and app frames stay responsive. */
    uint64_t now = c26_interrupt_ticks();
    /* Display heartbeat: repaint the whole scene once a second even with
       no damage, so a present dropped while the host window was still
       being created (seen with the cocoa display) heals instead of
       leaving a permanently blank screen. */
    static uint64_t last_heartbeat_tick;
    c26_screen_mode_t mode = c26_screen_mode();
    if (now - last_heartbeat_tick >= 100) {
        last_heartbeat_tick = now;
        if (mode == C26_SCREEN_GFX) {
            c26_framebuffer_present();
        } else {
            c26_compositor_mark_dirty();
        }
    }
    if (now - last_flush_tick >= 5 && mode != C26_SCREEN_GFX) {
        last_flush_tick = now;
        c26_compositor_flush();
    }
}

void c26_desktop_invalidate(void)
{
    c26_compositor_mark_dirty();
}
