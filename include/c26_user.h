#ifndef C26_USER_H
#define C26_USER_H

/* U-mode process support. The kernel stays in M-mode; a cartridge runs in
 * U-mode under an Sv39 address space and reaches the machine only through
 * these syscalls (issued by the user-mapped stub page that backs the
 * c26_api_t vector table). This is the whole surface — keep it one screen.
 */

#define C26_SYS_EXIT 0
#define C26_SYS_PUTS 1
#define C26_SYS_PUTC 2
#define C26_SYS_PUT_INT 3
#define C26_SYS_GETCHAR 4
#define C26_SYS_MOUSE 5
#define C26_SYS_STOP_REQUESTED 6
#define C26_SYS_TICKS 7
#define C26_SYS_YIELD 8
#define C26_SYS_FRAMEBUFFER 9
#define C26_SYS_PIXEL 10
#define C26_SYS_FILL_RECT 11
#define C26_SYS_DRAW_RECT 12
#define C26_SYS_LINE 13
#define C26_SYS_TEXT 14
#define C26_SYS_PRESENT 15
#define C26_SYS_VOICE_START 16
#define C26_SYS_VOICE_STOP 17
#define C26_SYS_FS_SAVE 18
#define C26_SYS_FS_LOAD 19
#define C26_SYS_FS_DELETE 20
#define C26_SYS_FS_COUNT 21
#define C26_SYS_FS_ENTRY 22
#define C26_SYS_DEV_READ8 23
#define C26_SYS_DEV_WRITE8 24
#define C26_SYS_COUNT 25

/* Trap frame layout: mepc at 0, x1..x31 at 8*N, kernel trap sp at 256,
 * kernel callee-saved context (ra, sp, s0-s11) at 264. trap.S depends on
 * these offsets. */
#define C26_FRAME_MEPC 0
#define C26_FRAME_KSP 256
#define C26_FRAME_KCTX 264

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct {
    uint64_t mepc;
    uint64_t regs[31]; /* x1..x31: regs[n-1] is xn */
    uint64_t kernel_sp;
    uint64_t kctx[14];
} c26_user_frame_t;

/* Register accessor: xN for N in 1..31. */
#define C26_FRAME_X(frame, n) ((frame)->regs[(n)-1])

extern c26_user_frame_t c26_user_frame;

/* Enters U-mode per the frame; returns only via c26_user_terminate with
 * that call's exit code. */
long c26_user_enter(c26_user_frame_t *frame);
void c26_user_terminate(c26_user_frame_t *frame, long code)
    __attribute__((noreturn));

/* Sv39 user address space: identity-maps the given user-visible ranges and
 * loads satp. The kernel itself stays bare (M-mode ignores satp). */
void c26_vm_reset(void);
int c26_vm_map_user(uint64_t base, uint64_t size, int writable,
                    int executable);
void c26_vm_activate(void);
int c26_vm_user_range(uint64_t base, uint64_t size, int write);

void c26_trap_handler_user(c26_user_frame_t *frame);

/* Shared interrupt bodies, callable from both kernel and user trap paths. */
void c26_timer_interrupt(void);
void c26_external_interrupt(void);

#endif

#endif
