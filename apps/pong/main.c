/* PONG: the IPC responder. Answers every "PING" message with "PONG" until
 * killed. Draws its state so the window shows life. */

#include "c26_api.h"

int app_main(const c26_api_t *api)
{
    if (api->version < 2) {
        return 1;
    }
    uint32_t width;
    uint32_t height;
    api->window_size(&width, &height);
    api->puts("PONG CART ONLINE\n");
    api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
    api->text(10, 10, "PONG: WAITING", 0xbac4ff, 0x0b1025, 2);
    api->present();

    for (;;) {
        if (api->stop_requested()) {
            return 0;
        }
        char buffer[16];
        int from = -1;
        int length = api->recv(&from, buffer, sizeof(buffer) - 1);
        if (length == 4 && buffer[0] == 'P' && buffer[1] == 'I' &&
            buffer[2] == 'N' && buffer[3] == 'G') {
            api->puts("PONG GOT PING\n");
            api->send(from, "PONG", 4);
            api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
            api->text(10, 10, "PONG: ANSWERED", 0x68f0c0, 0x0b1025, 2);
            api->present();
        }
        api->idle();
    }
}
