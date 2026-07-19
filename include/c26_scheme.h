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

/* The desktop seam: the embedder supplies the machine's real capabilities.
 * When left NULL, the graphics/audio/fs primitives emit textual traces
 * through the output sink (so the host tests stay hardware-free); the kernel
 * installs a platform that drives the actual c26 SDKs. This is the whole
 * point — every machine capability is one hook, exposed as one primitive. */
typedef struct {
    void (*color)(int c);
    void (*plot)(int x, int y);
    void (*line)(int x0, int y0, int x1, int y1);
    void (*rect)(int x, int y, int w, int h, int fill);
    void (*text)(int x, int y, const char *s);
    void (*cls)(void);
    void (*present)(void);
    void (*sound)(int voice, int freq);
    int (*fs_save)(const char *name, const char *data, int size);
    int (*fs_load)(const char *name, char *buf, int cap); /* len, or -1 */
    void (*screen)(int mode); /* 0 = text console, 1 = graphics */
} scm_platform_t;

void scm_set_platform(const scm_platform_t *platform);

/* Read and evaluate every form in `src`. When `echo` is set, the printed
 * form of each top-level result is emitted through the sink (REPL mode).
 * Returns 1 on success, 0 if a read or eval error was reported. */
int scm_eval_string(const char *src, int echo);

/* Reset the object arena between independent test programs. */
void scm_reset(void);

#endif
