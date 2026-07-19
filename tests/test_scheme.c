/* Host unit tests for the c26 Scheme core. The evaluator is pure logic, so
 * it compiles and runs on the host exactly like the eventual kernel build —
 * the same tests/ pattern used for basic.c and fs.c. */

#include "c26_scheme.h"

#include <stdio.h>
#include <string.h>

static char capture[65536];
static size_t capture_len;

static void sink(const char *text)
{
    size_t n = strlen(text);
    if (capture_len + n < sizeof(capture)) {
        memcpy(capture + capture_len, text, n);
        capture_len += n;
        capture[capture_len] = '\0';
    }
}

static int failures;

static void check(const char *program, const char *expected, const char *label)
{
    scm_reset();
    capture_len = 0;
    capture[0] = '\0';
    scm_eval_string(program, 1);
    if (strstr(capture, expected) == NULL) {
        failures++;
        printf("FAIL %-28s: %s\n  expected substring: %s\n  got: %s\n", label,
               program, expected, capture);
    }
}

int main(void)
{
    scm_set_output(sink);
    scm_init();

    /* Reader + arithmetic */
    check("(+ 1 2 3)", "6", "variadic add");
    check("(- 10 3 2)", "5", "left-fold sub");
    check("(- 5)", "-5", "unary negate");
    check("(* 2 3 4)", "24", "mul");
    check("(/ 100 5 2)", "10", "div");
    check("(modulo 17 5)", "2", "modulo");
    check("(+ (* 3 4) (- 10 2))", "20", "nested");
    check("-42", "-42", "negative literal");

    /* Booleans, comparisons, predicates */
    check("(< 1 2 3)", "#t", "chained lt true");
    check("(< 1 3 2)", "#f", "chained lt false");
    check("(= 7 7)", "#t", "num eq");
    check("(not #f)", "#t", "not");
    check("(zero? 0)", "#t", "zero?");
    check("(if (> 3 2) 'yes 'no)", "yes", "if true");
    check("(if (> 2 3) 'yes 'no)", "no", "if false");

    /* Lists + quote */
    check("'(1 2 3)", "(1 2 3)", "quote list");
    check("(cons 1 (cons 2 '()))", "(1 2)", "cons list");
    check("(car '(a b c))", "a", "car");
    check("(cdr '(a b c))", "(b c)", "cdr");
    check("(null? '())", "#t", "null?");
    check("(pair? '(1))", "#t", "pair?");
    check("(list 1 2 (+ 1 2))", "(1 2 3)", "list evals args");
    check("(cons 1 2)", "(1 . 2)", "dotted pair print");

    /* define, lambda, closures */
    check("(define x 10) (+ x 5)", "15", "define + use");
    check("(define (square n) (* n n)) (square 9)", "81", "define fn sugar");
    check("((lambda (a b) (+ a b)) 3 4)", "7", "lambda apply");
    check("(define (adder n) (lambda (x) (+ x n)))"
          "(define add5 (adder 5)) (add5 100)",
          "105", "closure captures env");
    check("(define (f . xs) xs) (f 1 2 3)", "(1 2 3)", "variadic rest");

    /* let, begin, set!, cond, and, or */
    check("(let ((a 2) (b 3)) (* a b))", "6", "let");
    check("(begin (define y 1) (set! y 9) y)", "9", "begin + set!");
    check("(cond ((= 1 2) 'a) ((= 1 1) 'b) (else 'c))", "b", "cond");
    check("(cond ((= 1 2) 'a) (else 'c))", "c", "cond else");
    check("(and 1 2 3)", "3", "and returns last");
    check("(and 1 #f 3)", "#f", "and short-circuits");
    check("(or #f 2 3)", "2", "or returns first true");

    /* Recursion + proper tail calls (must run in bounded space) */
    check("(define (fact n) (if (= n 0) 1 (* n (fact (- n 1))))) (fact 10)",
          "3628800", "recursive factorial");
    /* 5000 deep proves proper tail calls: a non-tail evaluator overflows
       the C stack long before this. The count is bounded only by the
       prototype's no-GC arena (each iteration conses a fresh arg list);
       the Cheney GC subtask removes that ceiling. */
    check("(define (loop i acc)"
          "  (if (= i 0) acc (loop (- i 1) (+ acc 1))))"
          "(loop 5000 0)",
          "5000", "tail-recursive loop (no C-stack blowup)");
    check("(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))"
          "(fib 15)",
          "610", "tree recursion");

    /* Garbage collection: these exceed the arena and force many GC cycles.
       If any conservative root were missed, results would corrupt and the
       expected substring would not appear. */
    check("(define (loop i acc)"
          "  (if (= i 0) acc (loop (- i 1) (+ acc 1))))"
          "(loop 200000 0)",
          "200000", "200k tail loop across many GCs");
    check("(define (sum n acc) (if (= n 0) acc (sum (- n 1) (+ acc n))))"
          "(sum 5000 0)",
          "12502500", "GC-heavy accumulation stays correct");
    /* Build and discard huge garbage lists, then verify a later computation
       is unaffected (the arena is reused after collection). */
    check("(define (build n) (if (= n 0) '() (cons n (build (- n 1)))))"
          "(car (build 3000))"
          "(car (build 3000))"
          "(+ 2 2)",
          "4", "garbage lists collected, later work correct");
    /* Interned symbols must survive collection (they are permanent roots);
       a value bound to one before the GC is still reachable after. */
    check("(define keep 'important)"
          "(define (churn n) (if (= n 0) 'done (begin (list n n n) (churn (- n 1)))))"
          "(churn 30000)"
          "keep",
          "important", "bound symbol survives GC churn");

    /* Higher-order: build map from primitives, use it */
    check("(define (map f xs)"
          "  (if (null? xs) '() (cons (f (car xs)) (map f (cdr xs)))))"
          "(map (lambda (x) (* x x)) '(1 2 3 4))",
          "(1 4 9 16)", "user-defined map");

    /* Escape continuations (call/cc) */
    check("(call/cc (lambda (k) 42))", "42", "call/cc normal return");
    check("(+ 1 (call/cc (lambda (k) (k 10) 999)))", "11", "call/cc escapes");
    check("(define (find pred xs)"
          "  (call/cc (lambda (return)"
          "    (begin"
          "      (define (go ys) (cond ((null? ys) #f)"
          "        ((pred (car ys)) (return (car ys)))"
          "        (else (go (cdr ys)))))"
          "      (go xs)))))"
          "(find (lambda (x) (> x 3)) '(1 2 3 4 5))",
          "4", "call/cc early exit from search");

    /* Desktop-as-primitives: the machine's APIs are just procedures */
    check("(color 14)", "[color 14]", "desktop primitive trace");
    check("(rect 10 20 100 50 3)", "[rect 10 20 100 50 3]", "rect primitive");
    check("(define (dashes n)"
          "  (if (> n 0) (begin (plot n (* n n)) (dashes (- n 1))) #t))"
          "(dashes 3)",
          "[plot 1 1]", "primitives driven by recursion");
    check("(for-each-demo)", "unbound variable", "error is reported, not crash");

    if (failures == 0) {
        printf("test_scheme: all assertions passed\n");
        return 0;
    }
    printf("test_scheme: %d failure(s)\n", failures);
    return 1;
}
