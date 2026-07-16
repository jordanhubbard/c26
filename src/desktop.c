#include "c26.h"
#include "c26_graphics.h"
#include "c26_input.h"

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
static int shift_down;
static const char *desktop_status = "READY";

static const char *app_name(int index)
{
    static const char *names[] = {"BASIC", "ROBOT LAB", "NETWORK", "DEVICES"};
    return names[index & 3];
}

static void draw_pointer(void)
{
    c26_draw_line(pointer_x, pointer_y, pointer_x, pointer_y + 14, 0xffffff);
    c26_draw_line(pointer_x, pointer_y, pointer_x + 9, pointer_y + 9, 0xffffff);
    c26_draw_line(pointer_x, pointer_y + 14, pointer_x + 4, pointer_y + 10, 0xffffff);
}

static void render_desktop(void)
{
    c26_fill_rect(0, 0, C26_SCREEN_WIDTH, C26_SCREEN_HEIGHT, 0x161b3c);
    c26_fill_rect(0, 0, C26_SCREEN_WIDTH, 34, 0x35409a);
    c26_draw_text(16, 9, "C26 DESKTOP", 0xffffff, 0x35409a, 2);
    c26_draw_text(474, 13, "QEMU VIRT", 0x9df6ff, 0x35409a, 1);

    static const uint32_t colors[] = {0x3d80ff, 0xff5ca8, 0x34c992, 0xffad45};
    for (int i = 0; i < 4; i++) {
        int x = 26 + i * 150;
        uint32_t border = i == selected_app ? 0xffffff : 0x6570bd;
        c26_fill_rect(x, 56, 132, 78, 0x222957);
        c26_draw_rect(x, 56, 132, 78, border);
        c26_fill_rect(x + 10, 68, 28, 28, colors[i]);
        c26_draw_text(x + 10, 110, app_name(i), 0xffffff, 0x222957, 1);
    }

    c26_fill_rect(24, 154, 284, 300, 0x0b1025);
    c26_draw_rect(24, 154, 284, 300, 0x7185ff);
    c26_fill_rect(25, 155, 282, 25, 0x29336f);
    c26_draw_text(36, 163, "BASIC TERMINAL", 0xffffff, 0x29336f, 1);
    c26_draw_text(38, 198, "C26 BASIC V1", 0x68f0c0, 0x0b1025, 1);
    c26_draw_text(38, 220, "TYPE HELP IN SERIAL", 0xbac4ff, 0x0b1025, 1);
    c26_draw_text(38, 242, "OR USE VIRTIO KEYS", 0xbac4ff, 0x0b1025, 1);
    c26_draw_text(38, 282, "DEVICE APIS", 0xffad45, 0x0b1025, 1);
    c26_draw_text(38, 302, "I2C CAN TCP INPUT", 0xe6e9ff, 0x0b1025, 1);
    c26_draw_text(38, 350, "STATUS", 0xff5ca8, 0x0b1025, 1);
    c26_draw_text(38, 372, desktop_status, 0xffffff, 0x0b1025, 1);
    c26_graphics_render_demo();
    draw_pointer();
    c26_framebuffer_present();
}

void c26_desktop_show(void)
{
    c26_framebuffer_init();
    c26_input_init();
    selected_app = 0;
    render_desktop();
    c26_puts("C26 DESKTOP: graphical shell online\n");
    c26_puts("DESKTOP INPUT: arrows select, Enter launches, mouse selects\n");
}

static void launch_selected(void)
{
    switch (selected_app) {
    case 0:
        desktop_status = "BASIC ACTIVE";
        c26_puts("[DESKTOP] BASIC active\n");
        break;
    case 1:
        desktop_status = "ROBOT DEMO RAN";
        c26_robot_demo();
        break;
    case 2:
        desktop_status = "NETWORK LOOPBACK";
        c26_puts("[DESKTOP] network loopback ready\n");
        break;
    default:
        desktop_status = "DEVICE FABRIC OK";
        c26_puts("[DESKTOP] device fabric ready\n");
        break;
    }
}

static int handle_input_event(c26_input_event_t event)
{
    if (event.type == C26_INPUT_EVENT_RELATIVE) {
        if (event.code == 0) pointer_x += event.value;
        if (event.code == 1) pointer_y += event.value;
        if (pointer_x < 0) pointer_x = 0;
        if (pointer_y < 34) pointer_y = 34;
        if (pointer_x >= (int)C26_SCREEN_WIDTH) pointer_x = C26_SCREEN_WIDTH - 1;
        if (pointer_y >= (int)C26_SCREEN_HEIGHT) pointer_y = C26_SCREEN_HEIGHT - 1;
        return 1;
    }
    if (event.type != C26_INPUT_EVENT_KEY) {
        return 0;
    }
    if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT) {
        shift_down = event.value != 0;
        return 0;
    }
    if (event.value == 0) {
        return 0;
    }
    if (event.code == BTN_LEFT) {
        if (pointer_y >= 56 && pointer_y < 134 && pointer_x >= 26) {
            int app = (pointer_x - 26) / 150;
            if (app >= 0 && app < 4) {
                selected_app = app;
                launch_selected();
            }
        }
        return 1;
    }
    if (event.code == KEY_LEFT || event.code == KEY_UP) {
        selected_app = (selected_app + 3) % 4;
        return 1;
    }
    if (event.code == KEY_RIGHT || event.code == KEY_DOWN) {
        selected_app = (selected_app + 1) % 4;
        return 1;
    }
    if (event.code == KEY_ENTER) {
        if (selected_app == 0) c26_basic_feed_char('\n');
        else launch_selected();
        return 1;
    }
    if (event.code == KEY_BACKSPACE) {
        c26_basic_feed_char('\b');
        return 0;
    }
    char ch = c26_input_key_to_ascii(event.code, shift_down);
    if (ch != 0 && selected_app == 0) {
        c26_basic_feed_char(ch);
    }
    return 0;
}

void c26_desktop_poll(void)
{
    int redraw = 0;
    int ch;
    while ((ch = c26_uart_getc_nonblocking()) >= 0) {
        c26_basic_feed_char((char)ch);
    }
    c26_input_event_t event;
    while (c26_input_poll(&event)) {
        redraw |= handle_input_event(event);
    }
    c26_audio_poll();
    if (redraw) {
        render_desktop();
    }
}
