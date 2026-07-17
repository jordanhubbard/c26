/* TICKER: prints a heartbeat forever. Exists to make multiprocessing
 * visible — its output interleaves with the console while you keep
 * working. KILL it from BASIC when you're done. */

#include "c26_api.h"

int app_main(const c26_api_t *api)
{
    api->puts("TICKER CART ONLINE\n");
    uint64_t last = api->ticks();
    for (;;) {
        if (api->stop_requested()) {
            return 0;
        }
        uint64_t now = api->ticks();
        if (now - last >= 50) {
            last = now;
            api->puts("TICK\n");
        }
        api->idle();
    }
}
