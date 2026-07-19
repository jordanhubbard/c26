/* RV64 setjmp/longjmp. Saves ra, sp, s0-s11; returns 0 from setjmp and the
 * (non-zero) value from longjmp. env layout matches c26_jmp_buf. */

    .section .text
    .globl c26_setjmp
c26_setjmp:
    sd ra,   0(a0)
    sd sp,   8(a0)
    sd s0,  16(a0)
    sd s1,  24(a0)
    sd s2,  32(a0)
    sd s3,  40(a0)
    sd s4,  48(a0)
    sd s5,  56(a0)
    sd s6,  64(a0)
    sd s7,  72(a0)
    sd s8,  80(a0)
    sd s9,  88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)
    li a0, 0
    ret

    .globl c26_longjmp
c26_longjmp:
    ld ra,   0(a0)
    ld sp,   8(a0)
    ld s0,  16(a0)
    ld s1,  24(a0)
    ld s2,  32(a0)
    ld s3,  40(a0)
    ld s4,  48(a0)
    ld s5,  56(a0)
    ld s6,  64(a0)
    ld s7,  72(a0)
    ld s8,  80(a0)
    ld s9,  88(a0)
    ld s10, 96(a0)
    ld s11, 104(a0)
    mv a0, a1
    bnez a0, 1f
    li a0, 1
1:
    ret
