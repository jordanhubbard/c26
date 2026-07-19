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

    /* Real-time clock */
    command("print time");
    expect("1752700000\n", "rtc seconds");
    command("t$ = time$");
    command("print t$");
    expect(":", "time$ formatted");

    /* String variables */
    command("new");
    command("a$ = \"hello\"");
    command("print a$");
    expect("HELLO\n", "string assign+print");
    command("b$ = a$ + \" world\"");
    command("print b$");
    expect("HELLO WORLD\n", "string concat");
    command("print \"x=\";a$");
    expect("X=HELLO\n", "print literal then string var");
    command("new");
    command("10 n$ = \"yes\"");
    command("20 if n$ = \"yes\" then print \"matched\"");
    command("30 if n$ <> \"no\" then print \"differs\"");
    command("40 if n$ = \"no\" then print \"bad\"");
    command("run");
    expect("MATCHED\n", "string if equal");
    expect("DIFFERS\n", "string if not-equal");
    expect_absent("BAD", "string if false branch");
    command("new");
    command("10 input s$");
    command("20 print \"got \";s$");
    shim_output_reset();
    shim_pending_input("WORLD\n");
    feed("run\n");
    expect("GOT WORLD\n", "string input");

    /* String functions: LEN LEFT$ RIGHT$ MID$ CHR$ ASC VAL STR$. */
    command("b$ = \"commodore\"");
    command("print len(b$)");
    expect("9\n", "LEN");
    command("print left$(b$,4)");
    expect("COMM\n", "LEFT$");
    command("print right$(b$,4)");
    expect("DORE\n", "RIGHT$");
    command("print mid$(b$,3,3)");
    expect("MMO\n", "MID$ with count");
    command("print mid$(b$,7)");
    expect("ORE\n", "MID$ to end");
    command("print chr$(65)+chr$(66)+chr$(67)");
    expect("ABC\n", "CHR$ + concat");
    command("print asc(\"Z\")");
    expect("90\n", "ASC");
    command("print val(\"123\")+1");
    expect("124\n", "VAL arithmetic");
    command("print str$(42)+\"!\"");
    expect("42!\n", "STR$ concat");
    command("print len(left$(b$,3)+right$(b$,2))");
    expect("5\n", "nested string funcs");
    command("print val(str$(7*6))");
    expect("42\n", "VAL of STR$ round-trip");

    /* Arrays (DIM) and user functions (DEF FN). */
    command("dim q(5)");
    command("q(2) = 99");
    command("print q(2)");
    expect("99\n", "array store/load");
    command("q(2) = q(2) + 1");
    command("print q(2)");
    expect("100\n", "array read-modify-write");
    command("print q(0)");
    expect("0\n", "array zero-initialized");
    command("print q(9)");
    expect("?BAD SUBSCRIPT ERROR", "array bounds check");
    command("print z(3)");            /* undimensioned auto-dims to 0..10 */
    expect("0\n", "array auto-dim");
    command("def fn d(x) = x*2+1");
    command("print fn d(10)");
    expect("21\n", "DEF FN call");
    command("print fn d(0)");
    expect("1\n", "DEF FN zero arg");
    command("print fn e(1)");
    expect("?UNDEF'D FUNCTION ERROR", "undefined function");

    /* Arrays and functions inside a program, and clean re-run. */
    command("new");
    command("10 dim m(4)");
    command("20 for i=0 to 4");
    command("30 m(i) = i*i");
    command("40 next");
    command("50 print m(3)");
    command("60 def fn t(n) = n*n+1");
    command("70 print fn t(m(2))");
    shim_output_reset();
    feed("run\n");
    expect("9\n", "array filled in a loop");
    expect("17\n", "DEF FN of an array element");
    shim_output_reset();
    feed("run\n");
    expect("9\n", "array survives re-run");
    expect_absent("REDIM", "re-run does not redim-error");

    /* DATA / READ / RESTORE. */
    command("new");
    command("10 data 10,20,30");
    command("20 data 40,\"hello\"");
    command("30 read a,b,c");
    command("40 print a+b+c");
    command("50 read d,e$");
    command("60 print d");
    command("70 print e$");
    command("80 restore");
    command("90 read f");
    command("100 print f");
    shim_output_reset();
    feed("run\n");
    expect("60\n", "READ three numbers");
    expect("40\n", "READ continues across DATA lines");
    expect("HELLO\n", "READ a string DATA item");
    expect("10\n", "RESTORE rewinds to the first DATA");

    command("new");
    command("10 data 1");
    command("20 read a,b");
    shim_output_reset();
    feed("run\n");
    expect("?OUT OF DATA ERROR", "READ past the end of DATA");

    command("new");
    command("10 data 1,2,3,4");
    command("20 dim v(3)");
    command("30 for i=0 to 3");
    command("40 read v(i)");
    command("50 next");
    command("60 print v(0)+v(3)");
    command("70 restore 10");
    command("80 read x");
    command("90 print x");
    shim_output_reset();
    feed("run\n");
    expect("5\n", "READ into array elements");
    expect("1\n", "RESTORE to a line number");

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
