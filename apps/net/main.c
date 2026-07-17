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

static void draw(void)
{
    ui_clear(&ui);
    ui_titlebar(&ui, "NET", "UDP 2601  Q QUIT");
    if (log_count == 0) {
        ui_text(&ui, 10, 30, "WAITING FOR DATAGRAMS...", UI_TEXT);
    }
    for (int i = 0; i < log_count; i++) {
        ui_text(&ui, 10, 30 + i * 14, log_lines[i], UI_GOOD);
    }
    ui_status(&ui, "EVERY DATAGRAM IS ACKED", UI_TEXT);
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
