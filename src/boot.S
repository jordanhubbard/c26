    .section .text.boot
    .globl _start

_start:
    la sp, _stack_top
    call kmain

1:
    wfi
    j 1b
