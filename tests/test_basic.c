/* Host-side tests for the BASIC interpreter: src/basic.c compiled as-is
 * against the shims, driven through c26_basic_feed_char exactly as the
 * machine drives it. */

#include "host_shim.h"

#include <stdio.h>
#include <string.h>

/* basic.c needs a filesystem; give it an offline one so it skips DEMO
 * seeding, and reject every operation (fs behavior is test_fs's job). */
#include "c26_fs.h"
int c26_fs_online(void) { return 0; }
size_t c26_fs_count(void) { return 0; }
int c26_fs_entry(size_t i, const char **n, uint32_t *s)
{
    (void)i; (void)n; (void)s;
    return 0;
}
int c26_fs_save(const char *n, const void *d, size_t s)
{
    (void)n; (void)d; (void)s;
    return 0;
}
int c26_fs_load(const char *n, void *d, size_t c, size_t *s)
{
    (void)n; (void)d; (void)c; (void)s;
    return 0;
}
int c26_fs_delete(const char *n) { (void)n; return 0; }
int c26_fs_rename(const char *o, const char *n)
{
    (void)o; (void)n;
    return 0;
}

#include "../src/basic.c"

static int failures;

static void feed(const char *text)
{
    while (*text != '\0') c26_basic_feed_char(*text++);
}

static void command(const char *line)
{
    shim_output_reset();
    feed(line);
    feed("\n");
}

static void expect(const char *needle, const char *label)
{
    if (strstr(shim_output, needle) == NULL) {
        failures++;
        printf("FAIL %s: expected %-24s got: %s\n", label, needle,
               shim_output);
    }
}

static void expect_absent(const char *needle, const char *label)
{
    if (strstr(shim_output, needle) != NULL) {
        failures++;
        printf("FAIL %s: must not contain %s\n", label, needle);
    }
}

int main(void)
{
    shim_pump_hook = c26_basic_feed_char;
    c26_basic_init();

    /* Expressions */
    command("print (3+4)*2");
    expect("14\n", "precedence");
    command("print 10-20");
    expect("-10\n", "signed");
    command("print 2*(3+4)-10/2");
    expect("9\n", "mixed");
    command("print 17 mod 5");
    expect("2\n", "mod");
    command("print abs(-42)");
    expect("42\n", "abs");
    command("print rnd(1)");
    expect("0\n", "rnd bound 1");
    command("print not 0");
    expect("-1\n", "not");
    command("print 1=1 and 2=2");
    expect("-1\n", "and");
    command("print 1>2 or 2>1");
    expect("-1\n", "or");
    command("print (2>1)*-5");
    expect("5\n", "compare times negative");
    command("print 1/0");
    expect("?DIVISION BY ZERO ERROR", "div by zero");
    command("print )");
    expect("?SYNTAX ERROR", "syntax");

    /* Variables */
    command("a=5");
    command("print a+1");
    expect("6\n", "implicit let");
    command("let b = a*2");
    command("print b");
    expect("10\n", "let");

    /* Control flow */
    command("new");
    command("10 for i=1 to 3");
    command("20 print i*i");
    command("30 next");
    command("40 print \"done\"");
    command("run");
    expect("1\n4\n9\nDONE\n", "for/next");

    command("new");
    command("10 gosub 100");
    command("20 print \"back\"");
    command("30 end");
    command("100 print \"sub\"");
    command("110 return");
    command("run");
    expect("SUB\nBACK\n", "gosub");

    command("new");
    command("10 if 2>1 then print \"yes\"");
    command("20 if 1>2 then print \"no\"");
    command("30 goto 50");
    command("40 print \"skipped\"");
    command("50 print \"end\"");
    command("run");
    expect("YES\n", "if true");
    expect_absent("NO\n", "if false");
    expect_absent("SKIPPED", "goto skips");
    expect("END\n", "goto lands");

    command("new");
    command("10 for i=1 to 2");
    command("20 for j=1 to 2");
    command("30 print i*10+j");
    command("40 next j");
    command("50 next i");
    command("run");
    expect("11\n12\n21\n22\n", "nested for");

    command("new");
    command("10 goto 999");
    command("run");
    expect("?UNDEFINED LINE ERROR IN 10", "undefined line");

    command("next");
    expect("?ILLEGAL DIRECT ERROR", "next direct");

    command("new");
    command("10 return");
    command("run");
    expect("?RETURN WITHOUT GOSUB ERROR IN 10", "return without gosub");

    /* Interaction */
    command("new");
    command("10 input a");
    command("20 print a*2");
    shim_output_reset();
    shim_pending_input("21\n");
    feed("run\n");
    expect("42\n", "input");

    command("new");
    command("10 get k");
    command("20 print k+1000");
    shim_output_reset();
    shim_pending_input("A");
    feed("run\n");
    expect("1065\n", "get");

    command("new");
    command("10 pause 5");
    command("20 print \"waited\"");
    command("run");
    expect("WAITED\n", "pause");

    /* Program editing */
    command("new");
    command("10 print 1");
    command("10");
    command("list");
    expect_absent("10 PRINT 1", "line delete");

    /* Line editor: cursor movement, mid-line insert/delete, history. */
    command("print 3\x1f""12"); /* left once, insert before the 3 */
    expect("123\n", "insert before cursor");
    command("print 45\b6");
    expect("46\n", "backspace mid-entry");
    shim_output_reset();
    feed("\x1c\n"); /* up-arrow recalls PRINT 46 */
    expect("46\n", "history recall");
    shim_output_reset();
    feed("print 8\x1b[D9\n"); /* ANSI left-arrow from a terminal */
    expect("98\n", "ansi escape arrows");

    /* Devices */
    command("device write 200 77");
    expect("DEVICE WRITE OK", "device write");
    command("device read 200");
    expect("DEVICE READ returned 77", "device read");
    command("poke 201,5");
    command("print peek(201)");
    expect("5\n", "peek function");
    command("sound 9,440");
    expect("?ILLEGAL QUANTITY ERROR", "sound voice range");

    if (failures == 0) {
        printf("test_basic: all assertions passed\n");
        return 0;
    }
    printf("test_basic: %d failure(s)\n", failures);
    return 1;
}
