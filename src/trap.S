#include "c26_user.h"

/* Trap entry for both worlds. mscratch is the discriminator: it holds the
 * user frame pointer while executing in U-mode and zero while in the
 * kernel. Offsets into the frame come from c26_user.h. */

    .section .text
    .align 4
    .globl c26_trap_entry

c26_trap_entry:
    csrrw sp, mscratch, sp
    bnez sp, trap_from_user
    /* Kernel trap: restore sp and use the interrupted kernel stack. */
    csrrw sp, mscratch, sp
    addi sp, sp, -256
    sd ra,   0(sp)
    sd gp,   8(sp)
    sd tp,  16(sp)
    sd t0,  24(sp)
    sd t1,  32(sp)
    sd t2,  40(sp)
    sd s0,  48(sp)
    sd s1,  56(sp)
    sd a0,  64(sp)
    sd a1,  72(sp)
    sd a2,  80(sp)
    sd a3,  88(sp)
    sd a4,  96(sp)
    sd a5, 104(sp)
    sd a6, 112(sp)
    sd a7, 120(sp)
    sd s2, 128(sp)
    sd s3, 136(sp)
    sd s4, 144(sp)
    sd s5, 152(sp)
    sd s6, 160(sp)
    sd s7, 168(sp)
    sd s8, 176(sp)
    sd s9, 184(sp)
    sd s10, 192(sp)
    sd s11, 200(sp)
    sd t3, 208(sp)
    sd t4, 216(sp)
    sd t5, 224(sp)
    sd t6, 232(sp)
    call c26_trap_handler
    ld ra,   0(sp)
    ld gp,   8(sp)
    ld tp,  16(sp)
    ld t0,  24(sp)
    ld t1,  32(sp)
    ld t2,  40(sp)
    ld s0,  48(sp)
    ld s1,  56(sp)
    ld a0,  64(sp)
    ld a1,  72(sp)
    ld a2,  80(sp)
    ld a3,  88(sp)
    ld a4,  96(sp)
    ld a5, 104(sp)
    ld a6, 112(sp)
    ld a7, 120(sp)
    ld s2, 128(sp)
    ld s3, 136(sp)
    ld s4, 144(sp)
    ld s5, 152(sp)
    ld s6, 160(sp)
    ld s7, 168(sp)
    ld s8, 176(sp)
    ld s9, 184(sp)
    ld s10, 192(sp)
    ld s11, 200(sp)
    ld t3, 208(sp)
    ld t4, 216(sp)
    ld t5, 224(sp)
    ld t6, 232(sp)
    addi sp, sp, 256
    mret

trap_from_user:
    /* sp = frame pointer; every other register is live user state. */
    sd x1,   8(sp)
    sd x3,  24(sp)
    sd x4,  32(sp)
    sd x5,  40(sp)
    sd x6,  48(sp)
    sd x7,  56(sp)
    sd x8,  64(sp)
    sd x9,  72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13, 104(sp)
    sd x14, 112(sp)
    sd x15, 120(sp)
    sd x16, 128(sp)
    sd x17, 136(sp)
    sd x18, 144(sp)
    sd x19, 152(sp)
    sd x20, 160(sp)
    sd x21, 168(sp)
    sd x22, 176(sp)
    sd x23, 184(sp)
    sd x24, 192(sp)
    sd x25, 200(sp)
    sd x26, 208(sp)
    sd x27, 216(sp)
    sd x28, 224(sp)
    sd x29, 232(sp)
    sd x30, 240(sp)
    sd x31, 248(sp)
    csrrw t0, mscratch, zero    /* t0 = user sp; kernel mode marked */
    sd t0, 16(sp)
    csrr t0, mepc
    sd t0, C26_FRAME_MEPC(sp)
    mv a0, sp
    /* Switch to this hart's kernel trap stack: c26_kernel_trap_sp[mhartid]. */
    csrr t2, mhartid
    slli t2, t2, 3
    la t1, c26_kernel_trap_sp
    add t1, t1, t2
    ld sp, 0(t1)
    call c26_trap_handler_user
    /* Resume whichever frame the scheduler selected for this hart. */
    csrr t2, mhartid
    slli t2, t2, 3
    la t6, c26_current_frame
    add t6, t6, t2
    ld t6, 0(t6)
    ld t0, C26_FRAME_MEPC(t6)
    csrw mepc, t0
    csrw mscratch, t6
    ld x1,   8(t6)
    ld x2,  16(t6)
    ld x3,  24(t6)
    ld x4,  32(t6)
    ld x5,  40(t6)
    ld x6,  48(t6)
    ld x7,  56(t6)
    ld x8,  64(t6)
    ld x9,  72(t6)
    ld x10, 80(t6)
    ld x11, 88(t6)
    ld x12, 96(t6)
    ld x13, 104(t6)
    ld x14, 112(t6)
    ld x15, 120(t6)
    ld x16, 128(t6)
    ld x17, 136(t6)
    ld x18, 144(t6)
    ld x19, 152(t6)
    ld x20, 160(t6)
    ld x21, 168(t6)
    ld x22, 176(t6)
    ld x23, 184(t6)
    ld x24, 192(t6)
    ld x25, 200(t6)
    ld x26, 208(t6)
    ld x27, 216(t6)
    ld x28, 224(t6)
    ld x29, 232(t6)
    ld x30, 240(t6)
    ld x31, 248(t6)
    mret

    /* long c26_user_enter(frame): save kernel context globally, drop to
     * U-mode. Returns only when c26_user_terminate restores it. */
    .globl c26_user_enter
c26_user_enter:
    /* No interrupts between here and mret: once mscratch is armed, a trap
       would record this kernel code as resumable user state. */
    li t0, 8
    csrc mstatus, t0
    /* Per-hart slots: t1 = mhartid, t3 = hart*8, save context at hart*112. */
    csrr t1, mhartid
    slli t3, t1, 3
    li t2, C26_KCTX_STRIDE
    mul t2, t1, t2
    la t0, c26_kernel_context
    add t0, t0, t2
    sd ra,  0(t0)
    sd sp,  8(t0)
    sd s0,  16(t0)
    sd s1,  24(t0)
    sd s2,  32(t0)
    sd s3,  40(t0)
    sd s4,  48(t0)
    sd s5,  56(t0)
    sd s6,  64(t0)
    sd s7,  72(t0)
    sd s8,  80(t0)
    sd s9,  88(t0)
    sd s10, 96(t0)
    sd s11, 104(t0)
    la t0, c26_kernel_trap_sp
    add t0, t0, t3
    sd sp, 0(t0)
    la t0, c26_current_frame
    add t0, t0, t3
    sd a0, 0(t0)
    /* mstatus: previous privilege U, previous interrupt-enable on. */
    li t0, (3 << 11)
    csrc mstatus, t0
    li t0, (1 << 7)
    csrs mstatus, t0
    ld t0, C26_FRAME_MEPC(a0)
    csrw mepc, t0
    csrw mscratch, a0
    /* Restore the complete register file: on re-entry after a time slice
       the frame holds live user state, not just the initial values. */
    mv t6, a0
    ld x1,   8(t6)
    ld x2,  16(t6)
    ld x3,  24(t6)
    ld x4,  32(t6)
    ld x5,  40(t6)
    ld x6,  48(t6)
    ld x7,  56(t6)
    ld x8,  64(t6)
    ld x9,  72(t6)
    ld x10, 80(t6)
    ld x11, 88(t6)
    ld x12, 96(t6)
    ld x13, 104(t6)
    ld x14, 112(t6)
    ld x15, 120(t6)
    ld x16, 128(t6)
    ld x17, 136(t6)
    ld x18, 144(t6)
    ld x19, 152(t6)
    ld x20, 160(t6)
    ld x21, 168(t6)
    ld x22, 176(t6)
    ld x23, 184(t6)
    ld x24, 192(t6)
    ld x25, 200(t6)
    ld x26, 208(t6)
    ld x27, 216(t6)
    ld x28, 224(t6)
    ld x29, 232(t6)
    ld x30, 240(t6)
    ld x31, 248(t6)
    mret

    /* void c26_user_terminate(code): unwind to c26_user_enter's caller.
       Called from trap context where the hardware cleared MIE — restore it
       so the kernel keeps taking interrupts between slices. */
    .globl c26_user_terminate
c26_user_terminate:
    csrw mscratch, zero
    li t0, 8
    csrs mstatus, t0
    /* Restore this hart's saved kernel context: c26_kernel_context[mhartid]. */
    csrr t1, mhartid
    li t2, C26_KCTX_STRIDE
    mul t2, t1, t2
    la t0, c26_kernel_context
    add t0, t0, t2
    ld ra,  0(t0)
    ld sp,  8(t0)
    ld s0,  16(t0)
    ld s1,  24(t0)
    ld s2,  32(t0)
    ld s3,  40(t0)
    ld s4,  48(t0)
    ld s5,  56(t0)
    ld s6,  64(t0)
    ld s7,  72(t0)
    ld s8,  80(t0)
    ld s9,  88(t0)
    ld s10, 96(t0)
    ld s11, 104(t0)
    ret
