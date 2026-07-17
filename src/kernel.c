#include "c26.h"
#include "c26_block.h"
#include "c26_console.h"
#include "c26_fs.h"
#include "c26_net.h"

void kmain(void)
{
    c26_puts("\nC26 RISC-V HOME COMPUTER\n");
    c26_puts("QEMU virt | rv64imac | freestanding C + assembly\n\n");

    c26_interrupts_init();
    c26_block_init();
    c26_fs_init();
    c26_net_init();
    c26_desktop_init();
    c26_graphics_demo();
    c26_audio_demo();
    c26_devices_demo();
    c26_robot_demo();

    c26_puts("C26 HARDWARE ONLINE\n");
    c26_puts("INTERRUPT ACTIVITY: timer=");
    c26_put_uint(c26_interrupt_ticks());
    c26_puts(" external=");
    c26_put_uint(c26_interrupt_external_count());
    c26_putc('\n');

    c26_puts("C26 INTERACTIVE LOOP ONLINE\n");
    /* The boot transcript stays on the serial console; the display boots
       to a clean screen like a home computer should. */
    c26_console_clear();
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    c26_basic_init();
    c26_console_flush();

    for (;;) {
        c26_desktop_poll();
        if (c26_cart_any_runnable()) {
            c26_cart_schedule();
        } else {
            c26_idle();
        }
    }
}
