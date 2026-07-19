#ifndef C26_SCHEME_H
#define C26_SCHEME_H

/* c26 Scheme — the reader + evaluator core.
 *
 * A small, integer-only Lisp whose primitives are the c26 desktop SDKs.
 * This file is freestanding-clean (no libc): all output flows through a
 * sink callback, so the host wires it to stdout and the kernel wires it to
 * c26_puts. The prototype uses a bump allocator; a Cheney copying GC is the
 * next subtask. Error recovery uses setjmp on the host; the kernel port
 * swaps in a ~15-line RISC-V setjmp/longjmp.
 */

void scm_init(void);

/* All terminal output (printer, `display`, desktop stubs) goes here. */
void scm_set_output(void (*sink)(const char *text));

/* Read and evaluate every form in `src`. When `echo` is set, the printed
 * form of each top-level result is emitted through the sink (REPL mode).
 * Returns 1 on success, 0 if a read or eval error was reported. */
int scm_eval_string(const char *src, int echo);

/* Reset the object arena between independent test programs. */
void scm_reset(void);

#endif
