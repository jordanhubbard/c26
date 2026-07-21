#include "c26_user.h"

    .section .text.boot
    .globl _start

/* With -bios none, every QEMU hart resets into _start together. Hart 0 boots
 * the machine; harts 1..C26_NHART-1 take their own stack, wait for hart 0 to
 * finish init (c26_secondary_go), then enter their scheduler loop. Any hart
 * id >= C26_NHART just parks. */
_start:
    csrr t0, mhartid
    li   t1, C26_NHART
    bgeu t0, t1, park          /* id >= C26_NHART: unsupported, park */
    bnez t0, secondary         /* id != 0: secondary hart */

    /* Hart 0: the boot hart. */
    la sp, _stack_top
    call kmain
    j park

secondary:
    /* sp = top of this hart's slab in hart_stacks[id]. */
    la   sp, hart_stacks
    li   t2, C26_HART_STACK
    mul  t3, t0, t2
    add  sp, sp, t3
    add  sp, sp, t2
    /* Wait until hart 0 releases us. */
    la   t4, c26_secondary_go
1:  lw   t5, 0(t4)
    beqz t5, 1b
    fence
    call c26_secondary_main

park:
    wfi
    j park

    .section .bss
    .align 4
hart_stacks:
    .skip C26_NHART * C26_HART_STACK
