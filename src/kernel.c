#include "c26.h"
#include "c26_block.h"
#include "c26_console.h"
#include "c26_fs.h"
#include "c26_net.h"

/* Released by hart 0 once init is done; secondary harts spin on it in boot.S. */
volatile int c26_secondary_go;

void kmain(void)
{
    c26_puts("\nC26 RISC-V HOME COMPUTER\n");
    c26_puts("QEMU virt | rv64imac | freestanding C + assembly\n\n");

    c26_interrupts_init();
    c26_block_init();
    c26_fs_init();
    c26_dock_rebuild();
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

    /* SMP: release the secondary harts, let them announce themselves, and
       report how many cores came online. Hart 0 owns the desktop from here. */
    c26_hart_mark_online();
    c26_secondary_go = 1;
    __asm__ volatile("fence rw, rw");
    uint64_t start = c26_interrupt_ticks();
    while (c26_interrupt_ticks() - start < 3) { /* ~30ms for secondaries */ }
    c26_puts("SMP: ");
    c26_put_uint((uint64_t)c26_hart_count());
    c26_puts(" harts online\n");

    for (;;) {
        /* Hart 0's interactive work (console, compositor, BASIC, devices) runs
           under the big kernel lock; app scheduling drops it around U-mode.
           Hart 0 also reaps processes that exited on a secondary hart, so all
           focus/console/interpreter side effects of teardown stay on hart 0. */
        c26_kernel_lock();
        c26_cart_reap_exited();
        c26_desktop_poll();
        c26_kernel_unlock();
        if (c26_cart_any_runnable()) {
            c26_cart_schedule();
        } else {
            c26_idle();
        }
    }
}
