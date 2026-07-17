#include "c26_user.h"

/* The user stub page: the only kernel-image page mapped (read/execute) into
 * a cartridge's address space. Each stub raises one syscall; the api table
 * at the end backs c26_api_t, in exact field order. Everything must stay
 * within a single 4 KiB page. */

#define STUB(name, number) \
    .align 3 ; \
name: ; \
    li a7, number ; \
    ecall ; \
    ret

    .section .user_stubs, "ax"
    .align 12
    .globl c26_user_stub_base
c26_user_stub_base:

    .align 3
    .globl c26_user_stub_exit
c26_user_stub_exit:
    li a7, C26_SYS_EXIT
    ecall
1:  j 1b

STUB(stub_puts, C26_SYS_PUTS)
STUB(stub_putc, C26_SYS_PUTC)
STUB(stub_put_int, C26_SYS_PUT_INT)
STUB(stub_getchar, C26_SYS_GETCHAR)
STUB(stub_mouse, C26_SYS_MOUSE)
STUB(stub_stop_requested, C26_SYS_STOP_REQUESTED)
STUB(stub_ticks, C26_SYS_TICKS)
STUB(stub_yield, C26_SYS_YIELD)
STUB(stub_framebuffer, C26_SYS_FRAMEBUFFER)
STUB(stub_pixel, C26_SYS_PIXEL)
STUB(stub_fill_rect, C26_SYS_FILL_RECT)
STUB(stub_draw_rect, C26_SYS_DRAW_RECT)
STUB(stub_line, C26_SYS_LINE)
STUB(stub_text, C26_SYS_TEXT)
STUB(stub_present, C26_SYS_PRESENT)
STUB(stub_voice_start, C26_SYS_VOICE_START)
STUB(stub_voice_stop, C26_SYS_VOICE_STOP)
STUB(stub_fs_save, C26_SYS_FS_SAVE)
STUB(stub_fs_load, C26_SYS_FS_LOAD)
STUB(stub_fs_delete, C26_SYS_FS_DELETE)
STUB(stub_fs_count, C26_SYS_FS_COUNT)
STUB(stub_fs_entry, C26_SYS_FS_ENTRY)
STUB(stub_dev_read8, C26_SYS_DEV_READ8)
STUB(stub_dev_write8, C26_SYS_DEV_WRITE8)
STUB(stub_window_size, C26_SYS_WINDOW_SIZE)
STUB(stub_send, C26_SYS_SEND)
STUB(stub_recv, C26_SYS_RECV)
STUB(stub_spawn, C26_SYS_SPAWN)
STUB(stub_udp_bind, C26_SYS_UDP_BIND)
STUB(stub_udp_send, C26_SYS_UDP_SEND)
STUB(stub_udp_recv, C26_SYS_UDP_RECV)

    /* c26_api_t, field for field. */
    .align 3
    .globl c26_user_api
c26_user_api:
    .word 2                    /* version */
    .word 0                    /* padding */
    .quad stub_puts
    .quad stub_putc
    .quad stub_put_int
    .quad stub_getchar
    .quad stub_mouse
    .quad stub_stop_requested
    .quad stub_ticks
    .quad stub_yield           /* api->idle */
    .quad stub_framebuffer
    .quad stub_pixel
    .quad stub_fill_rect
    .quad stub_draw_rect
    .quad stub_line
    .quad stub_text
    .quad stub_present
    .quad stub_voice_start
    .quad stub_voice_stop
    .quad stub_fs_save
    .quad stub_fs_load
    .quad stub_fs_delete
    .quad stub_fs_count
    .quad stub_fs_entry
    .quad stub_dev_read8
    .quad stub_dev_write8
    .quad stub_window_size
    .quad stub_send
    .quad stub_recv
    .quad stub_spawn
    .quad stub_udp_bind
    .quad stub_udp_send
    .quad stub_udp_recv
