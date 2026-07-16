#include "c26.h"
#include "c26_console.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_input.h"

#define KEY_ESC 1U
#define KEY_BACKSPACE 14U
#define KEY_ENTER 28U
#define KEY_LEFTSHIFT 42U
#define KEY_RIGHTSHIFT 54U
#define KEY_UP 103U
#define KEY_LEFT 105U
#define KEY_RIGHT 106U
#define KEY_DOWN 108U
#define BTN_LEFT 272U

static int selected_app;
static int pointer_x = 310;
static int pointer_y = 230;
static int pointer_buttons;
static int shift_down;
static int redraw_requested;
static const char *desktop_status = "READY";

static const char *app_name(int index)
{
    static const char *names[] = {"BASIC", "FILES", "ROBOT", "NETWORK", "DEVICES"};
    return names[index % 5];
}

static void draw_pointer(void)
{
    c26_draw_line(pointer_x, pointer_y, pointer_x, pointer_y + 14, 0xffffff);
    c26_draw_line(pointer_x, pointer_y, pointer_x + 9, pointer_y + 9, 0xffffff);
    c26_draw_line(pointer_x, pointer_y + 14, pointer_x + 4, pointer_y + 10, 0xffffff);
}

static void file_label(char *label, const char *name, uint32_t size)
{
    size_t used = 0;
    while (*name != '\0' && used < 20) label[used++] = *name++;
    label[used++] = ' ';
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + size % 10U);
        size /= 10U;
    } while (size != 0 && count < sizeof(digits));
    while (count != 0) label[used++] = digits[--count];
    label[used++] = ' ';
    label[used++] = 'B';
    label[used] = '\0';
}

static void render_desktop(void)
{
    c26_fill_rect(0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT, 0x161b3c);
    c26_fill_rect(0, 0, C26_SCREEN_WIDTH, 34, 0x35409a);
    c26_draw_text(16, 9, "C26 DESKTOP", 0xffffff, 0x35409a, 2);
    c26_draw_text(474, 13, "QEMU VIRT", 0x9df6ff, 0x35409a, 1);

    static const uint32_t colors[] = {
        0x3d80ff, 0xffd34d, 0xff5ca8, 0x34c992, 0xffad45};
    for (int i = 0; i < 5; i++) {
        int x = 12 + i * 125;
        uint32_t border = i == selected_app ? 0xffffff : 0x6570bd;
        c26_fill_rect(x, 56, 114, 78, 0x222957);
        c26_draw_rect(x, 56, 114, 78, border);
        c26_fill_rect(x + 10, 68, 28, 28, colors[i]);
        c26_draw_text(x + 10, 110, app_name(i), 0xffffff, 0x222957, 1);
    }

    c26_fill_rect(24, 154, 284, 300, 0x0b1025);
    c26_draw_rect(24, 154, 284, 300, 0x7185ff);
    c26_fill_rect(25, 155, 282, 25, 0x29336f);
    if (selected_app == 1) {
        c26_draw_text(36, 163, "C26FS FILE BROWSER", 0xffffff, 0x29336f, 1);
        size_t count = c26_fs_count();
        if (count == 0) {
            c26_draw_text(38, 198, "NO SAVED PROGRAMS", 0xbac4ff, 0x0b1025, 1);
            c26_draw_text(38, 220, "USE SAVE NAME", 0xffd34d, 0x0b1025, 1);
        }
        for (size_t i = 0; i < count; i++) {
            const char *name;
            uint32_t size;
            if (c26_fs_entry(i, &name, &size)) {
                char label[32];
                file_label(label, name, size);
                c26_fill_rect(36, 192 + (int)i * 20, 250, 17,
                              i == 0 ? 0x263168 : 0x151b38);
                c26_draw_text(42, 198 + (int)i * 20, label,
                              0xffffff, i == 0 ? 0x263168 : 0x151b38, 1);
            }
        }
    } else {
        c26_draw_text(36, 163, "BASIC TERMINAL", 0xffffff, 0x29336f, 1);
        c26_draw_text(38, 198, "C26 BASIC V3", 0x68f0c0, 0x0b1025, 1);
        c26_draw_text(38, 220, "ENTER LAUNCHES BASIC", 0xbac4ff, 0x0b1025, 1);
        c26_draw_text(38, 242, "ESC RETURNS HERE", 0xffd34d, 0x0b1025, 1);
        c26_draw_text(38, 282, "DEVICE APIS", 0xffad45, 0x0b1025, 1);
        c26_draw_text(38, 302, "I2C CAN TCP INPUT", 0xe6e9ff, 0x0b1025, 1);
        c26_draw_text(38, 350, "STATUS", 0xff5ca8, 0x0b1025, 1);
        c26_draw_text(38, 372, desktop_status, 0xffffff, 0x0b1025, 1);
    }
    c26_graphics_render_demo();
    draw_pointer();
    c26_framebuffer_present();
}

void c26_desktop_init(void)
{
    c26_framebuffer_init();
    c26_input_init();
    selected_app = 0;
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    c26_puts("C26 DESKTOP: graphical shell online\n");
    c26_puts("DESKTOP INPUT: Esc opens desktop, arrows select, Enter launches\n");
}

static void enter_console(const char *status)
{
    desktop_status = status;
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    c26_console_flush();
}

static void launch_selected(void)
{
    switch (selected_app) {
    case 0:
        c26_puts("[DESKTOP] BASIC active\n");
        enter_console("BASIC ACTIVE");
        break;
    case 1:
        desktop_status = "C26FS BROWSER";
        c26_puts("[DESKTOP] C26FS file browser active\n");
        redraw_requested = 1;
        break;
    case 2:
        desktop_status = "ROBOT DEMO RAN";
        c26_robot_demo();
        redraw_requested = 1;
        break;
    case 3:
        desktop_status = "NETWORK LOOPBACK";
        c26_puts("[DESKTOP] network loopback ready\n");
        redraw_requested = 1;
        break;
    default:
        desktop_status = "DEVICE FABRIC OK";
        c26_puts("[DESKTOP] device fabric ready\n");
        redraw_requested = 1;
        break;
    }
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

static int desktop_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_RELATIVE) {
        move_pointer(event, 34);
        return 1;
    }
    if (event.type != C26_INPUT_EVENT_KEY || event.value == 0) {
        return 0;
    }
    if (event.code == KEY_ESC) {
        enter_console("BASIC ACTIVE");
        return 0;
    }
    if (event.code == BTN_LEFT) {
        if (pointer_y >= 56 && pointer_y < 134 && pointer_x >= 12) {
            int app = (pointer_x - 12) / 125;
            if (app >= 0 && app < 5) {
                selected_app = app;
                launch_selected();
            }
        }
        return 1;
    }
    if (event.code == KEY_LEFT || event.code == KEY_UP) {
        selected_app = (selected_app + 4) % 5;
        return 1;
    }
    if (event.code == KEY_RIGHT || event.code == KEY_DOWN) {
        selected_app = (selected_app + 1) % 5;
        return 1;
    }
    if (event.code == KEY_ENTER) {
        launch_selected();
        return 1;
    }
    return 0;
}

static void cart_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_RELATIVE) {
        move_pointer(event, 0);
        return;
    }
    if (event.type != C26_INPUT_EVENT_KEY || event.value == 0 ||
        event.code == BTN_LEFT) {
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
    char ch = c26_input_key_to_ascii(event.code, shift_down);
    if (ch != 0) {
        c26_basic_feed_char(ch);
    }
}

static void console_event(c26_input_event_t event)
{
    if (event.type != C26_INPUT_EVENT_KEY || event.value == 0) {
        return;
    }
    if (event.code == KEY_ESC) {
        if (c26_basic_running()) {
            c26_basic_feed_char(0x1b);
        } else {
            c26_screen_set_mode(C26_SCREEN_DESKTOP);
            redraw_requested = 1;
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
    case C26_SCREEN_DESKTOP:
        redraw_requested |= desktop_event(event);
        break;
    case C26_SCREEN_CART:
        cart_event(event);
        break;
    case C26_SCREEN_CONSOLE:
    case C26_SCREEN_GFX:
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
}

void c26_desktop_poll(void)
{
    static uint64_t last_flush_tick;
    c26_io_pump();
    if (c26_screen_mode() == C26_SCREEN_DESKTOP && redraw_requested) {
        redraw_requested = 0;
        render_desktop();
    }
    /* Presenting the console is a full-screen GPU transfer; cap it at
       one render per 5 timer ticks so typing bursts stay responsive. */
    uint64_t now = c26_interrupt_ticks();
    if (c26_console_dirty() && now - last_flush_tick >= 5) {
        last_flush_tick = now;
        c26_console_flush();
    }
}

void c26_desktop_invalidate(void)
{
    if (c26_screen_mode() == C26_SCREEN_DESKTOP) {
        redraw_requested = 1;
    }
}
