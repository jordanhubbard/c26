#include "c26.h"

void kmain(void)
{
    c26_puts("\nC26 RISC-V HOME COMPUTER\n");
    c26_puts("QEMU virt | rv64imac | freestanding C + assembly\n\n");

    c26_desktop_show();
    c26_graphics_demo();
    c26_audio_demo();
    c26_devices_demo();
    c26_basic_demo();
    c26_robot_demo();

    c26_puts("C26 DEMO COMPLETE\n");

    for (;;) {
        __asm__ volatile("wfi");
    }
}
