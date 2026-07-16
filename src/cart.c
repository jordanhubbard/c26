#include "c26.h"
#include "c26_api.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_user.h"

/* Cartridges execute in U-mode inside an Sv39 address space that identity-
 * maps only what belongs to them: the load region, a private stack, the
 * framebuffer surface, one scratch page, and the read/execute syscall stub
 * page. Everything else — kernel, MMIO, other memory — faults. The
 * c26_api_t contract is unchanged from ABI v1: its function pointers now
 * land in the stub page, which raises syscalls handled here. */

#define USER_STACK_BYTES (64U * 1024U)

extern char c26_user_stub_base[];
extern char c26_user_stub_exit[];
extern char c26_user_api[];

c26_user_frame_t c26_user_frame;
static uint8_t user_stack[USER_STACK_BYTES] __attribute__((aligned(4096)));
static char user_scratch[4096] __attribute__((aligned(4096)));
static int cart_active;

static uint64_t read_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static uint64_t read_mtval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
}

static int user_string_ok(uint64_t address)
{
    for (uint64_t end = address; ; end++) {
        if (!c26_vm_user_range(end, 1, 0)) {
            return 0;
        }
        if (*(const char *)(uintptr_t)end == '\0') {
            return 1;
        }
    }
}

static void kill_process(c26_user_frame_t *frame, const char *reason,
                         long code)
{
    c26_puts("CART ");
    c26_puts(reason);
    c26_putc('\n');
    c26_user_terminate(frame, code);
}

static long do_syscall(c26_user_frame_t *frame)
{
    uint64_t number = C26_FRAME_X(frame, 17);
    uint64_t a0 = C26_FRAME_X(frame, 10);
    uint64_t a1 = C26_FRAME_X(frame, 11);
    uint64_t a2 = C26_FRAME_X(frame, 12);
    uint64_t a3 = C26_FRAME_X(frame, 13);
    uint64_t a4 = C26_FRAME_X(frame, 14);
    uint64_t a5 = C26_FRAME_X(frame, 15);

    switch (number) {
    case C26_SYS_EXIT:
        c26_user_terminate(frame, (long)(int64_t)a0);
    case C26_SYS_PUTS:
        if (!user_string_ok(a0)) break;
        c26_puts((const char *)(uintptr_t)a0);
        return 0;
    case C26_SYS_PUTC:
        c26_putc((char)a0);
        return 0;
    case C26_SYS_PUT_INT:
        c26_put_int((int64_t)a0);
        return 0;
    case C26_SYS_GETCHAR: {
        char ch;
        return c26_basic_key_pop(&ch) ? (long)(uint8_t)ch : -1;
    }
    case C26_SYS_MOUSE: {
        int x;
        int y;
        int buttons;
        c26_desktop_mouse(&x, &y, &buttons);
        if (a0 != 0) {
            if (!c26_vm_user_range(a0, 4, 1)) break;
            *(int *)(uintptr_t)a0 = x;
        }
        if (a1 != 0) {
            if (!c26_vm_user_range(a1, 4, 1)) break;
            *(int *)(uintptr_t)a1 = y;
        }
        if (a2 != 0) {
            if (!c26_vm_user_range(a2, 4, 1)) break;
            *(int *)(uintptr_t)a2 = buttons;
        }
        return 0;
    }
    case C26_SYS_STOP_REQUESTED:
        return c26_basic_break_requested();
    case C26_SYS_TICKS:
        return (long)c26_interrupt_ticks();
    case C26_SYS_YIELD:
        c26_io_pump();
        c26_idle();
        return 0;
    case C26_SYS_FRAMEBUFFER:
        return (long)(uintptr_t)c26_framebuffer_pixels();
    case C26_SYS_PIXEL:
        c26_draw_pixel((int)a0, (int)a1, (uint32_t)a2);
        return 0;
    case C26_SYS_FILL_RECT:
        c26_fill_rect((int)a0, (int)a1, (int)a2, (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_DRAW_RECT:
        c26_draw_rect((int)a0, (int)a1, (int)a2, (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_LINE:
        c26_draw_line((int)a0, (int)a1, (int)a2, (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_TEXT:
        if (!user_string_ok(a2)) break;
        c26_draw_text((int)a0, (int)a1, (const char *)(uintptr_t)a2,
                      (uint32_t)a3, (uint32_t)a4, (unsigned int)a5);
        return 0;
    case C26_SYS_PRESENT:
        c26_framebuffer_present();
        return 0;
    case C26_SYS_VOICE_START:
        if ((int64_t)a1 < 0 || a1 > 3) return 0;
        return c26_audio_voice_start((unsigned int)a0, (c26_waveform_t)a1,
                                     (uint32_t)a2, (uint8_t)a3, (uint8_t)a4);
    case C26_SYS_VOICE_STOP:
        c26_audio_voice_stop((unsigned int)a0);
        return 0;
    case C26_SYS_FS_SAVE:
        if (!user_string_ok(a0) || !c26_vm_user_range(a1, a2, 0)) break;
        return c26_fs_save((const char *)(uintptr_t)a0,
                           (const void *)(uintptr_t)a1, a2);
    case C26_SYS_FS_LOAD: {
        if (!user_string_ok(a0) || !c26_vm_user_range(a1, a2, 1)) break;
        size_t size = 0;
        long ok = c26_fs_load((const char *)(uintptr_t)a0,
                              (void *)(uintptr_t)a1, a2, &size);
        if (a3 != 0) {
            if (!c26_vm_user_range(a3, 8, 1)) break;
            *(uint64_t *)(uintptr_t)a3 = size;
        }
        return ok;
    }
    case C26_SYS_FS_DELETE:
        if (!user_string_ok(a0)) break;
        return c26_fs_delete((const char *)(uintptr_t)a0);
    case C26_SYS_FS_COUNT:
        return (long)c26_fs_count();
    case C26_SYS_FS_ENTRY: {
        const char *name;
        uint32_t size;
        if (!c26_fs_entry(a0, &name, &size)) return 0;
        size_t length = 0;
        while (name[length] != '\0' && length < sizeof(user_scratch) - 1) {
            user_scratch[length] = name[length];
            length++;
        }
        user_scratch[length] = '\0';
        if (a1 != 0) {
            if (!c26_vm_user_range(a1, 8, 1)) break;
            *(uint64_t *)(uintptr_t)a1 = (uint64_t)(uintptr_t)user_scratch;
        }
        if (a2 != 0) {
            if (!c26_vm_user_range(a2, 4, 1)) break;
            *(uint32_t *)(uintptr_t)a2 = size;
        }
        return 1;
    }
    case C26_SYS_DEV_READ8: {
        uint8_t value = 0;
        if (!c26_vm_user_range(a1, 1, 1)) break;
        long ok = c26_device_read8((uint16_t)a0, &value);
        *(uint8_t *)(uintptr_t)a1 = value;
        return ok;
    }
    case C26_SYS_DEV_WRITE8:
        return c26_device_write8((uint16_t)a0, (uint8_t)a1);
    default:
        break;
    }
    kill_process(frame, "FAULT bad syscall or pointer", -1);
    return 0;
}

void c26_trap_handler_user(c26_user_frame_t *frame)
{
    uint64_t cause = read_mcause();
    if ((cause & (1ULL << 63)) != 0) {
        cause &= ~(1ULL << 63);
        if (cause == 7) {
            c26_timer_interrupt();
        } else if (cause == 11) {
            c26_external_interrupt();
        }
        /* The kernel owns the machine again on every interrupt: keep input
           and audio alive even if the app spins, and kill it on demand. */
        c26_io_pump();
        if (c26_basic_break_requested()) {
            kill_process(frame, "KILLED", -3);
        }
        return;
    }
    if (cause == 8) { /* ecall from U-mode */
        frame->mepc += 4;
        C26_FRAME_X(frame, 10) = (uint64_t)do_syscall(frame);
        return;
    }
    c26_puts("CART FAULT cause=");
    c26_put_hex(cause);
    c26_puts(" addr=");
    c26_put_hex(read_mtval());
    c26_putc('\n');
    c26_user_terminate(frame, -1);
}

int c26_cart_run(const char *name)
{
    if (cart_active) {
        return -1;
    }
    uint8_t *base = (uint8_t *)C26_CART_BASE;
    size_t size = 0;
    if (!c26_fs_load(name, base, C26_CART_MAX_BYTES, &size)) {
        c26_puts("Error: cartridge load failed\n");
        return -1;
    }
    const c26_cart_header_t *header = (const c26_cart_header_t *)base;
    if (size < sizeof(*header) || header->magic != C26_CART_MAGIC ||
        header->version != C26_CART_VERSION || header->load_size > size ||
        header->entry_offset >= header->load_size ||
        header->load_size + (uint64_t)header->bss_size > C26_CART_MAX_BYTES) {
        c26_puts("Error: not a c26 cartridge\n");
        return -1;
    }
    memset(base + header->load_size, 0, header->bss_size);
    __asm__ volatile("fence.i");

    c26_vm_reset();
    uint32_t *pixels = c26_framebuffer_pixels();
    if (!c26_vm_map_user(C26_CART_BASE, C26_CART_MAX_BYTES, 1, 1) ||
        !c26_vm_map_user((uint64_t)(uintptr_t)user_stack, USER_STACK_BYTES,
                         1, 0) ||
        !c26_vm_map_user((uint64_t)(uintptr_t)user_scratch,
                         sizeof(user_scratch), 1, 0) ||
        !c26_vm_map_user((uint64_t)(uintptr_t)pixels,
                         C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT * 4, 1, 0) ||
        !c26_vm_map_user((uint64_t)(uintptr_t)c26_user_stub_base, 4096, 0,
                         1)) {
        c26_puts("Error: cartridge address space setup failed\n");
        return -1;
    }
    c26_vm_activate();

    memset(&c26_user_frame, 0, sizeof(c26_user_frame));
    c26_user_frame.mepc = C26_CART_BASE + header->entry_offset;
    C26_FRAME_X(&c26_user_frame, 1) =
        (uint64_t)(uintptr_t)c26_user_stub_exit;                /* ra */
    C26_FRAME_X(&c26_user_frame, 2) =
        (uint64_t)(uintptr_t)user_stack + USER_STACK_BYTES;     /* sp */
    C26_FRAME_X(&c26_user_frame, 10) =
        (uint64_t)(uintptr_t)c26_user_api;                      /* a0 */

    c26_screen_mode_t previous_mode = c26_screen_mode();
    c26_basic_set_external_consumer(1);
    c26_basic_clear_break();
    c26_screen_set_mode(C26_SCREEN_CART);
    cart_active = 1;
    c26_puts("CART START ");
    c26_puts(name);
    c26_putc('\n');

    long result = c26_user_enter(&c26_user_frame);

    cart_active = 0;
    c26_basic_set_external_consumer(0);
    c26_basic_clear_break();
    c26_screen_set_mode(previous_mode == C26_SCREEN_DESKTOP
                            ? C26_SCREEN_DESKTOP
                            : C26_SCREEN_CONSOLE);
    c26_puts("CART EXIT ");
    c26_put_int(result);
    c26_putc('\n');
    c26_console_flush();
    /* Leftover type-ahead returns to the line editor. */
    char ch;
    while (!c26_basic_running() && c26_basic_key_pop(&ch)) {
        c26_basic_feed_char(ch);
    }
    return (int)result;
}
