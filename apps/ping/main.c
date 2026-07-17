/* PING: the IPC initiator. Sends "PING" to every other job until one
 * answers "PONG", then reports the round trip and exits. */

#include "c26_api.h"

int app_main(const c26_api_t *api)
{
    if (api->version < 2) {
        return 1;
    }
    api->puts("PING CART ONLINE\n");
    uint64_t last_send = 0;
    for (;;) {
        if (api->stop_requested()) {
            return 1;
        }
        uint64_t now = api->ticks();
        if (now - last_send >= 20) {
            last_send = now;
            for (int job = 0; job < 4; job++) {
                api->send(job, "PING", 4);
            }
        }
        char buffer[16];
        int from = -1;
        int length = api->recv(&from, buffer, sizeof(buffer) - 1);
        if (length == 4 && buffer[0] == 'P' && buffer[1] == 'O' &&
            buffer[2] == 'N' && buffer[3] == 'G') {
            api->puts("IPC ROUNDTRIP OK FROM JOB ");
            api->put_int(from);
            api->putc('\n');
            return 0;
        }
        api->idle();
    }
}
