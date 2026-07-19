#ifndef C26_SETJMP_H
#define C26_SETJMP_H

/* Freestanding setjmp/longjmp for the kernel (RV64 has no libc). The buffer
 * holds ra, sp, and the callee-saved registers s0-s11 — which also makes it
 * the exact register spill the Scheme GC scans conservatively. */

typedef unsigned long c26_jmp_buf[14];

int c26_setjmp(c26_jmp_buf env);
void c26_longjmp(c26_jmp_buf env, int value) __attribute__((noreturn));

#endif
