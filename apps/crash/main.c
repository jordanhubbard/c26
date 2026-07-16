/* CRASH: deliberately dereferences kernel memory. Under M3 protection the
 * store faults, the kernel kills this process, and the machine lives. */

#include "c26_api.h"

int app_main(const c26_api_t *api)
{
    api->puts("CRASH CART ONLINE\n");
    *(volatile unsigned int *)0x80000000UL = 0xdead;
    api->puts("CRASH CART SURVIVED - PROTECTION IS BROKEN\n");
    return 2;
}
