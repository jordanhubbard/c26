#include "c26.h"
#include "c26_api.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_fs.h"
#include "c26_graphics.h"
#include "c26_user.h"

/* The process host. Up to C26_NPROC cartridges run concurrently, each a
 * U-mode process with its own Sv39 space and its own 640x480 surface. All
 * cartridges link at C26_CART_BASE; page tables map that VA to a different
 * physical slot per process. The kernel schedules round-robin time slices
 * from the main loop; the timer trap ends a slice, and faults, exits,
 * Ctrl-C, and KILL end a process. The compositor blits the focused
 * process's surface to the scanout with a status bar; Ctrl-T/Tab move
 * focus without stopping anything. */

#define C26_NPROC 4
#define USER_STACK_BYTES (64U * 1024U)
#define SLICE_TICKS 3
#define EXIT_SLICE 0x7fff0001L
#define SURFACE_PIXELS (C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT)

extern char c26_user_stub_base[];
extern char c26_user_stub_exit[];
extern char c26_user_api[];

typedef enum {
    PROC_FREE = 0,
    PROC_RUNNABLE = 1,
} proc_state_t;

typedef struct {
    proc_state_t state;
    char name[C26_FS_NAME_MAX + 1];
    c26_user_frame_t frame;
    c26_vm_space_t space;
    int kill_pending;
    int surface_damaged;
} proc_t;

c26_user_frame_t *c26_current_frame;
uint64_t c26_kernel_trap_sp;
uint64_t c26_kernel_context[14];

static proc_t procs[C26_NPROC];
static uint8_t proc_stack[C26_NPROC][USER_STACK_BYTES]
    __attribute__((aligned(4096)));
static char proc_scratch[C26_NPROC][4096] __attribute__((aligned(4096)));
static uint32_t proc_surface[C26_NPROC][SURFACE_PIXELS]
    __attribute__((aligned(4096)));

static int current = -1;
static int focused = -1;
static int rr_cursor;
static int compose_needed;
static volatile int slice_ticks_left;

/* ------------------------------------------------------------------ */
/* Surface primitives (draw into a process surface, clipped)           */

static void surface_pixel(uint32_t *surface, int x, int y, uint32_t color)
{
    if (x >= 0 && y >= 0 && x < (int)C26_SCREEN_WIDTH &&
        y < (int)C26_SCREEN_HEIGHT) {
        surface[(unsigned int)y * C26_SCREEN_WIDTH + (unsigned int)x] = color;
    }
}

static void surface_fill(uint32_t *surface, int x, int y, int width,
                         int height, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)C26_SCREEN_WIDTH) x1 = C26_SCREEN_WIDTH;
    if (y1 > (int)C26_SCREEN_HEIGHT) y1 = C26_SCREEN_HEIGHT;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            surface[(unsigned int)py * C26_SCREEN_WIDTH + (unsigned int)px] =
                color;
        }
    }
}

static void surface_line(uint32_t *surface, int x0, int y0, int x1, int y1,
                         uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        surface_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void surface_text(uint32_t *surface, int x, int y, const char *text,
                         uint32_t fg, uint32_t bg, unsigned int scale)
{
    if (scale == 0) scale = 1;
    while (*text != '\0') {
        const uint8_t *glyph = c26_font_glyph(*text++);
        surface_fill(surface, x, y, (int)(6 * scale), (int)(8 * scale), bg);
        for (unsigned int column = 0; column < 5; column++) {
            for (unsigned int row = 0; row < 7; row++) {
                if ((glyph[column] & (1U << row)) != 0) {
                    surface_fill(surface, x + (int)(column * scale),
                                 y + (int)(row * scale), (int)scale,
                                 (int)scale, fg);
                }
            }
        }
        x += (int)(6 * scale);
    }
}

/* ------------------------------------------------------------------ */
/* Focus and the compositor                                            */

static void drain_typeahead(void)
{
    /* Stops as soon as a replayed line hands the queue to a new consumer
       (a BASIC RUN or another spawn), else pop/feed would cycle forever. */
    char ch;
    while (!c26_basic_running() && !c26_basic_queue_consumed_externally() &&
           c26_basic_key_pop(&ch)) {
        c26_basic_feed_char(ch);
    }
}

void c26_cart_focus_console(void)
{
    focused = -1;
    c26_basic_set_external_consumer(0);
    c26_screen_set_mode(C26_SCREEN_CONSOLE);
    drain_typeahead();
    c26_console_flush();
}

static void focus_proc(int index)
{
    focused = index;
    c26_basic_set_external_consumer(1);
    c26_screen_set_mode(C26_SCREEN_CART);
    compose_needed = 1;
}

void c26_cart_focus_next(void)
{
    int start = focused;
    for (int step = 1; step <= C26_NPROC; step++) {
        int candidate = start + step;
        if (candidate >= C26_NPROC) {
            c26_cart_focus_console();
            return;
        }
        if (procs[candidate].state == PROC_RUNNABLE) {
            focus_proc(candidate);
            return;
        }
    }
}

void c26_compositor_flush(void)
{
    if (focused < 0 || c26_screen_mode() != C26_SCREEN_CART) {
        return;
    }
    proc_t *process = &procs[focused];
    if (!process->surface_damaged && !compose_needed) {
        return;
    }
    process->surface_damaged = 0;
    compose_needed = 0;
    uint32_t *pixels = c26_framebuffer_pixels();
    memcpy(pixels, proc_surface[focused], SURFACE_PIXELS * 4);
    int bar_y = (int)C26_SCREEN_HEIGHT - 12;
    c26_fill_rect(0, bar_y, (int)C26_SCREEN_WIDTH, 12, 0x222957);
    char label[64];
    size_t used = 0;
    label[used++] = 'J';
    label[used++] = 'O';
    label[used++] = 'B';
    label[used++] = ' ';
    label[used++] = (char)('0' + focused);
    label[used++] = ' ';
    for (const char *n = process->name; *n != '\0' && used < 24; n++) {
        label[used++] = *n;
    }
    label[used] = '\0';
    c26_draw_text(6, bar_y + 2, label, 0xffffff, 0x222957, 1);
    c26_draw_text(300, bar_y + 2, "TAB NEXT  CTRL-T CONSOLE  CTRL-C KILL",
                  0x9df6ff, 0x222957, 1);
    c26_framebuffer_present();
}

/* ------------------------------------------------------------------ */
/* Syscalls                                                            */

static uintptr_t user_ptr(uint64_t va, uint64_t size, int write)
{
    if (current < 0) {
        return 0;
    }
    return c26_vm_translate(&procs[current].space, va, size, write);
}

static const char *user_string(uint64_t va)
{
    uintptr_t base = user_ptr(va, 1, 0);
    if (base == 0) {
        return 0;
    }
    for (uint64_t length = 1; length <= 4096; length++) {
        if (user_ptr(va, length, 0) == 0) {
            return 0;
        }
        if (*(const char *)(base + length - 1) == '\0') {
            return (const char *)base;
        }
    }
    return 0;
}

static uint64_t read_csr_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static uint64_t read_csr_mtval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
}

static long do_syscall(c26_user_frame_t *frame)
{
    proc_t *process = &procs[current];
    uint64_t number = C26_FRAME_X(frame, 17);
    uint64_t a0 = C26_FRAME_X(frame, 10);
    uint64_t a1 = C26_FRAME_X(frame, 11);
    uint64_t a2 = C26_FRAME_X(frame, 12);
    uint64_t a3 = C26_FRAME_X(frame, 13);
    uint64_t a4 = C26_FRAME_X(frame, 14);
    uint64_t a5 = C26_FRAME_X(frame, 15);

    switch (number) {
    case C26_SYS_EXIT:
        c26_user_terminate((long)(int64_t)a0);
    case C26_SYS_PUTS: {
        const char *text = user_string(a0);
        if (text == 0) break;
        c26_puts(text);
        return 0;
    }
    case C26_SYS_PUTC:
        c26_putc((char)a0);
        return 0;
    case C26_SYS_PUT_INT:
        c26_put_int((int64_t)a0);
        return 0;
    case C26_SYS_GETCHAR: {
        char ch;
        if (current != focused) return -1;
        return c26_basic_key_pop(&ch) ? (long)(uint8_t)ch : -1;
    }
    case C26_SYS_MOUSE: {
        int x;
        int y;
        int buttons;
        c26_desktop_mouse(&x, &y, &buttons);
        if (current != focused) buttons = 0;
        uintptr_t px = a0 != 0 ? user_ptr(a0, 4, 1) : 1;
        uintptr_t py = a1 != 0 ? user_ptr(a1, 4, 1) : 1;
        uintptr_t pb = a2 != 0 ? user_ptr(a2, 4, 1) : 1;
        if (px == 0 || py == 0 || pb == 0) break;
        if (a0 != 0) *(int *)px = x;
        if (a1 != 0) *(int *)py = y;
        if (a2 != 0) *(int *)pb = buttons;
        return 0;
    }
    case C26_SYS_STOP_REQUESTED:
        return process->kill_pending || c26_basic_break_requested();
    case C26_SYS_TICKS:
        return (long)c26_interrupt_ticks();
    case C26_SYS_YIELD:
        /* Give up the rest of the slice. */
        c26_user_terminate(EXIT_SLICE);
    case C26_SYS_FRAMEBUFFER:
        return (long)(uintptr_t)proc_surface[current];
    case C26_SYS_PIXEL:
        surface_pixel(proc_surface[current], (int)a0, (int)a1, (uint32_t)a2);
        return 0;
    case C26_SYS_FILL_RECT:
        surface_fill(proc_surface[current], (int)a0, (int)a1, (int)a2,
                     (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_DRAW_RECT:
        surface_fill(proc_surface[current], (int)a0, (int)a1, (int)a2, 1,
                     (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0, (int)a1 + (int)a3 - 1,
                     (int)a2, 1, (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0, (int)a1, 1, (int)a3,
                     (uint32_t)a4);
        surface_fill(proc_surface[current], (int)a0 + (int)a2 - 1, (int)a1,
                     1, (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_LINE:
        surface_line(proc_surface[current], (int)a0, (int)a1, (int)a2,
                     (int)a3, (uint32_t)a4);
        return 0;
    case C26_SYS_TEXT: {
        const char *text = user_string(a2);
        if (text == 0) break;
        surface_text(proc_surface[current], (int)a0, (int)a1, text,
                     (uint32_t)a3, (uint32_t)a4, (unsigned int)a5);
        return 0;
    }
    case C26_SYS_PRESENT:
        process->surface_damaged = 1;
        return 0;
    case C26_SYS_VOICE_START:
        if ((int64_t)a1 < 0 || a1 > 3) return 0;
        return c26_audio_voice_start((unsigned int)a0, (c26_waveform_t)a1,
                                     (uint32_t)a2, (uint8_t)a3, (uint8_t)a4);
    case C26_SYS_VOICE_STOP:
        c26_audio_voice_stop((unsigned int)a0);
        return 0;
    case C26_SYS_FS_SAVE: {
        const char *name = user_string(a0);
        uintptr_t data = user_ptr(a1, a2, 0);
        if (name == 0 || data == 0) break;
        return c26_fs_save(name, (const void *)data, a2);
    }
    case C26_SYS_FS_LOAD: {
        const char *name = user_string(a0);
        uintptr_t data = user_ptr(a1, a2, 1);
        if (name == 0 || data == 0) break;
        size_t size = 0;
        long ok = c26_fs_load(name, (void *)data, a2, &size);
        if (a3 != 0) {
            uintptr_t out = user_ptr(a3, 8, 1);
            if (out == 0) break;
            *(uint64_t *)out = size;
        }
        return ok;
    }
    case C26_SYS_FS_DELETE: {
        const char *name = user_string(a0);
        if (name == 0) break;
        return c26_fs_delete(name);
    }
    case C26_SYS_FS_COUNT:
        return (long)c26_fs_count();
    case C26_SYS_FS_ENTRY: {
        const char *name;
        uint32_t size;
        if (!c26_fs_entry(a0, &name, &size)) return 0;
        char *scratch = proc_scratch[current];
        size_t length = 0;
        while (name[length] != '\0' && length < 4095) {
            scratch[length] = name[length];
            length++;
        }
        scratch[length] = '\0';
        if (a1 != 0) {
            uintptr_t out = user_ptr(a1, 8, 1);
            if (out == 0) break;
            *(uint64_t *)out = (uint64_t)(uintptr_t)scratch;
        }
        if (a2 != 0) {
            uintptr_t out = user_ptr(a2, 4, 1);
            if (out == 0) break;
            *(uint32_t *)out = size;
        }
        return 1;
    }
    case C26_SYS_DEV_READ8: {
        uintptr_t out = user_ptr(a1, 1, 1);
        if (out == 0) break;
        uint8_t value = 0;
        long ok = c26_device_read8((uint16_t)a0, &value);
        *(uint8_t *)out = value;
        return ok;
    }
    case C26_SYS_DEV_WRITE8:
        return c26_device_write8((uint16_t)a0, (uint8_t)a1);
    default:
        break;
    }
    c26_puts("CART FAULT bad syscall or pointer\n");
    c26_user_terminate(-1);
}

void c26_trap_handler_user(c26_user_frame_t *frame)
{
    uint64_t cause = read_csr_mcause();
    if ((cause & (1ULL << 63)) != 0) {
        cause &= ~(1ULL << 63);
        if (cause == 7) {
            c26_timer_interrupt();
            slice_ticks_left--;
        } else if (cause == 11) {
            c26_external_interrupt();
        }
        /* The kernel owns the machine on every interrupt: input and audio
           survive a spinning app, and kill requests land immediately. */
        c26_io_pump();
        if (c26_basic_break_requested() && focused >= 0) {
            procs[focused].kill_pending = 1;
            c26_basic_clear_break();
        }
        if (current >= 0 && procs[current].kill_pending) {
            c26_user_terminate(-3);
        }
        if (slice_ticks_left <= 0) {
            c26_user_terminate(EXIT_SLICE);
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
    c26_put_hex(read_csr_mtval());
    c26_putc('\n');
    c26_user_terminate(-1);
}

/* ------------------------------------------------------------------ */
/* Process lifecycle and scheduling                                    */

static void finalize(int index, long code)
{
    procs[index].state = PROC_FREE;
    procs[index].kill_pending = 0;
    if (code == -3) {
        c26_puts("CART KILLED\n");
    }
    c26_puts("CART EXIT ");
    c26_put_int(code);
    c26_putc('\n');
    if (focused == index) {
        c26_cart_focus_console();
    }
}

int c26_cart_run(const char *name)
{
    int slot = -1;
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        c26_puts("Error: all job slots are busy (see JOBS)\n");
        return -1;
    }
    uint8_t *base = (uint8_t *)(uintptr_t)(C26_CART_BASE +
                                           (uint64_t)slot * C26_CART_MAX_BYTES);
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

    proc_t *process = &procs[slot];
    c26_vm_init(&process->space);
    if (!c26_vm_map(&process->space, C26_CART_BASE, (uint64_t)(uintptr_t)base,
                    C26_CART_MAX_BYTES, 1, 1) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_stack[slot],
                    (uint64_t)(uintptr_t)proc_stack[slot], USER_STACK_BYTES,
                    1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_scratch[slot],
                    (uint64_t)(uintptr_t)proc_scratch[slot], 4096, 1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)proc_surface[slot],
                    (uint64_t)(uintptr_t)proc_surface[slot],
                    SURFACE_PIXELS * 4, 1, 0) ||
        !c26_vm_map(&process->space, (uint64_t)(uintptr_t)c26_user_stub_base,
                    (uint64_t)(uintptr_t)c26_user_stub_base, 4096, 0, 1)) {
        c26_puts("Error: cartridge address space setup failed\n");
        return -1;
    }

    size_t name_length = 0;
    while (name[name_length] != '\0' && name_length < C26_FS_NAME_MAX) {
        process->name[name_length] = name[name_length];
        name_length++;
    }
    process->name[name_length] = '\0';
    memset(proc_surface[slot], 0, SURFACE_PIXELS * 4);
    memset(&process->frame, 0, sizeof(process->frame));
    process->frame.mepc = C26_CART_BASE + header->entry_offset;
    C26_FRAME_X(&process->frame, 1) =
        (uint64_t)(uintptr_t)c26_user_stub_exit;
    C26_FRAME_X(&process->frame, 2) =
        (uint64_t)(uintptr_t)proc_stack[slot] + USER_STACK_BYTES;
    C26_FRAME_X(&process->frame, 10) = (uint64_t)(uintptr_t)c26_user_api;
    process->kill_pending = 0;
    process->surface_damaged = 0;
    process->state = PROC_RUNNABLE;

    c26_puts("CART START ");
    c26_puts(name);
    c26_puts(" AS JOB ");
    c26_put_uint((uint64_t)slot);
    c26_putc('\n');
    focus_proc(slot);
    return slot;
}

int c26_cart_any_runnable(void)
{
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_RUNNABLE) {
            return 1;
        }
    }
    return 0;
}

int c26_cart_kill(int job)
{
    if (job < 0 || job >= C26_NPROC || procs[job].state != PROC_RUNNABLE) {
        return 0;
    }
    procs[job].kill_pending = 1;
    return 1;
}

void c26_cart_list_jobs(void)
{
    int any = 0;
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state != PROC_RUNNABLE) continue;
        any = 1;
        c26_puts("  JOB ");
        c26_put_uint((uint64_t)i);
        c26_puts("  ");
        c26_puts(procs[i].name);
        c26_puts(i == focused ? "  [FOCUSED]\n" : "\n");
    }
    if (!any) {
        c26_puts("  (NO JOBS)\n");
    }
}

void c26_cart_schedule(void)
{
    /* Finalize kills requested against processes that are not running. */
    for (int i = 0; i < C26_NPROC; i++) {
        if (procs[i].state == PROC_RUNNABLE && procs[i].kill_pending) {
            finalize(i, -3);
        }
    }
    int next = -1;
    for (int step = 0; step < C26_NPROC; step++) {
        int candidate = (rr_cursor + step) % C26_NPROC;
        if (procs[candidate].state == PROC_RUNNABLE) {
            next = candidate;
            break;
        }
    }
    if (next < 0) {
        return;
    }
    rr_cursor = (next + 1) % C26_NPROC;
    current = next;
    slice_ticks_left = SLICE_TICKS;
    c26_vm_activate(&procs[next].space);
    long code = c26_user_enter(&procs[next].frame);
    current = -1;
    if (code != EXIT_SLICE) {
        finalize(next, code);
    }
}
