/* SPIN: an app that hangs — no yield, no getchar, no exit. Preemption must
 * keep the machine's input and audio alive and Ctrl-C must kill it. */

#include "c26_api.h"

int app_main(const c26_api_t *api)
{
    api->puts("SPIN CART ONLINE\n");
    for (;;) {
    }
}
