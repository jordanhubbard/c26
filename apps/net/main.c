/* NET: the network mailbox. Binds UDP port 2601, shows every datagram
 * that arrives, and answers each one with ACK:<payload> — reachable from
 * the host through QEMU's hostfwd. Q quits. */

#include "ui.h"

#define NET_PORT 2601
#define LOG_LINES 12

static c26_ui_t ui;
static char log_lines[LOG_LINES][44];
static int log_count;

static void log_add(const char *text)
{
    if (log_count == LOG_LINES) {
        for (int i = 1; i < LOG_LINES; i++) {
            for (int j = 0; j < 44; j++) log_lines[i - 1][j] = log_lines[i][j];
        }
        log_count--;
    }
    int i = 0;
    while (text[i] != '\0' && i < 43) {
        log_lines[log_count][i] = text[i];
        i++;
    }
    log_lines[log_count][i] = '\0';
    log_count++;
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

static void draw(void)
{
    const c26_api_t *api = ui.api;
    int w = (int)ui.width;
    int h = (int)ui.height;

    /* subtle window background gradient */
    vgrad(api, 0, 0, w, h, 0x141a36, 0x0a0e20);

    /* glossy title bar */
    vgrad(api, 0, 0, w, 30, 0x30397e, 0x191f48);
    api->fill_rect(0, 0, w, 1, 0x4a56b4);  /* bright top highlight */
    api->fill_rect(0, 29, w, 1, 0x0a0e20); /* dark seam shadow */
    label_fg(api, 8, 5, "NET", 0xffffff, 0x30397e, 3);
    {
        const char *hint = "UDP 2601  Q QUIT";
        int length = 0;
        while (hint[length] != '\0') length++;
        label_fg(api, w - 12 * length - 8, 9, hint, 0xbac4ff, 0x30397e, 2);
    }

    if (log_count == 0) {
        label_fg(api, 10, 30, "WAITING FOR DATAGRAMS...", 0xbac4ff, 0x0a0e20, 2);
    }
    for (int i = 0; i < log_count; i++) {
        int y = 30 + i * 14;
        int last = (i == log_count - 1);
        if (last) { /* accent the most recent arrival */
            vgrad(api, 4, y, w - 8, 14, 0x4653b4, 0x2f3894);
            api->fill_rect(4, y, w - 8, 1, 0x5a67c8); /* top highlight line */
        }
        label_fg(api, 10, y, log_lines[i], last ? 0xffffff : 0x68f0c0,
                 last ? 0x4653b4 : 0x0a0e20, 2);
    }

    /* glossy status line */
    {
        int y = h - 24;
        vgrad(api, 0, y, w, 24, 0x30397e, 0x191f48);
        api->fill_rect(0, y, w, 1, 0x4a56b4);      /* bright top highlight */
        api->fill_rect(0, y + 23, w, 1, 0x0a0e20); /* dark seam shadow */
        label_fg(api, 8, y + 5, "EVERY DATAGRAM IS ACKED", 0xbac4ff, 0x30397e, 2);
    }
    ui.dirty = 1;
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    ui_init(&ui, api);
    api->puts("NET CART ONLINE\n");
    if (!api->udp_bind(NET_PORT)) {
        api->puts("NET: bind failed\n");
        return 1;
    }
    draw();

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) return 0;
        if (key == 'Q' || key == 0x1b) {
            api->puts("NET CART EXIT\n");
            return 0;
        }
        char payload[64];
        uint32_t from_ip = 0;
        uint16_t from_port = 0;
        int received = api->udp_recv(NET_PORT, &from_ip, &from_port, payload,
                                     sizeof(payload) - 5);
        if (received >= 0) {
            payload[received] = '\0';
            api->puts("NET RX ");
            api->puts(payload);
            api->putc('\n');
            char line[52] = "RX ";
            int i = 0;
            while (payload[i] != '\0' && i < 40) {
                line[3 + i] = payload[i];
                i++;
            }
            line[3 + i] = '\0';
            log_add(line);
            char reply[70] = "ACK:";
            for (int j = 0; j <= received; j++) reply[4 + j] = payload[j];
            api->udp_send(from_ip, from_port, NET_PORT, reply,
                          (size_t)(received + 4));
            draw();
        }
        ui_flush(&ui);
    }
}
