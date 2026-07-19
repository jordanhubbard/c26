/* Host REPL for c26 Scheme — a way to play with the interpreter core
 * before it is wired into the machine. Reads lines from stdin, evaluates,
 * prints. This driver is the only part that touches libc; scheme.c stays
 * freestanding-clean. */

#include "c26_scheme.h"

#include <stdio.h>
#include <string.h>

static void to_stdout(const char *text)
{
    fputs(text, stdout);
}

int main(int argc, char **argv)
{
    scm_set_output(to_stdout);
    scm_init();

    if (argc > 1) {
        /* Evaluate each argument as a program (handy for one-liners). */
        for (int i = 1; i < argc; i++) {
            scm_eval_string(argv[i], 1);
        }
        return 0;
    }

    fputs("c26 Scheme (prototype). Ctrl-D to exit.\n", stdout);
    char line[4096];
    for (;;) {
        fputs("\xce\xbb> ", stdout); /* λ> */
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            fputs("\n", stdout);
            break;
        }
        scm_eval_string(line, 1);
    }
    return 0;
}
