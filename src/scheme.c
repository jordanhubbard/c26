/* c26 Scheme — reader + evaluator core (prototype).
 *
 * Integer-only Lisp. Freestanding-clean: no libc, all I/O via a sink. The
 * value representation uses immediate fixnums (low-bit tag) and boxed
 * pairs/symbols/strings/primitives/closures in a fixed arena. The evaluator
 * is a tree walker with a tail-position trampoline, giving proper tail
 * calls so a `(define (loop) ... (loop))` runs in constant space.
 *
 * Prototype scope: reader, printer, evaluator with the core special forms,
 * and a primitive table that includes desktop stubs (plot/rect/... emit
 * textual traces through the sink) to demonstrate that the machine's APIs
 * become Scheme primitives. Not yet: garbage collection, quasiquote,
 * call/cc — each is its own scoped subtask.
 */

#include "c26_scheme.h"

#include <setjmp.h> /* host prototype; kernel provides a tiny riscv setjmp */
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Values: immediate fixnums + boxed objects                           */

typedef uintptr_t value;

#define FIXNUM_TAG 1
#define IS_FIXNUM(v) (((v) & 1) == FIXNUM_TAG)
#define MAKE_FIXNUM(n) ((value)(((intptr_t)(n) << 1) | FIXNUM_TAG))
#define FIXNUM_VALUE(v) ((intptr_t)(v) >> 1)

typedef enum {
    T_NIL,
    T_BOOL,
    T_PAIR,
    T_SYMBOL,
    T_STRING,
    T_PRIMITIVE,
    T_CLOSURE,
    T_EOF,
    T_UNSPEC,
} obj_type;

typedef struct obj obj_t;
typedef value (*primitive_fn)(value args);

struct obj {
    obj_type type;
    union {
        struct {
            value car;
            value cdr;
        } pair;
        struct {
            const char *name;
        } symbol;
        struct {
            const char *chars;
            int length;
        } string;
        struct {
            primitive_fn fn;
            const char *name;
        } primitive;
        struct {
            value params;
            value body;
            value env;
        } closure;
        int boolean;
    } as;
};

/* Singletons — their addresses are the sentinel values. */
static obj_t nil_obj = {T_NIL, {{0, 0}}};
static obj_t true_obj = {T_BOOL, {{0, 0}}};
static obj_t false_obj = {T_BOOL, {{0, 0}}};
static obj_t eof_obj = {T_EOF, {{0, 0}}};
static obj_t unspec_obj = {T_UNSPEC, {{0, 0}}};

#define NIL ((value)&nil_obj)
#define TRUE ((value)&true_obj)
#define FALSE ((value)&false_obj)
#define EOF_VAL ((value)&eof_obj)
#define UNSPEC ((value)&unspec_obj)

#define IS_OBJ(v) (!IS_FIXNUM(v))
#define OBJ(v) ((obj_t *)(v))
#define TYPE(v) (OBJ(v)->type)

/* ------------------------------------------------------------------ */
/* Arena (bump allocator; GC is the next subtask)                      */

#define ARENA_OBJECTS (1 << 16)
#define STRING_POOL_BYTES (1 << 16)
#define SYMBOL_MAX 4096

static obj_t arena[ARENA_OBJECTS];
static int free_next[ARENA_OBJECTS]; /* free-list links (by index) */
static int free_head;
static unsigned char marks[ARENA_OBJECTS];
static char string_pool[STRING_POOL_BYTES];
static unsigned int string_pool_top;
static value symbols[SYMBOL_MAX];
static unsigned int symbol_count;
static value global_env; /* a GC root; defined during scm_init */
static char *stack_bottom;
static unsigned int gc_cycles;

static void (*output)(const char *);

static jmp_buf error_jump;
static const char *error_message;

static void scm_error(const char *message)
{
    error_message = message;
    longjmp(error_jump, 1);
}

/* --- Garbage collector: conservative mark-sweep -------------------- *
 *
 * Non-moving, so values held in C locals stay valid — we find them by
 * scanning the C stack (and spilled registers) for word-aligned bit
 * patterns that land exactly on an arena slot. Conservative scanning can
 * only ever RETAIN a false positive; it can never free a live object or
 * corrupt one, because nothing moves. Immediate fixnums have their low bit
 * set, so they are never mistaken for aligned object pointers, and the
 * singletons live outside the arena, so they are ignored. Precise roots
 * (the global environment and the permanent symbol table) are marked
 * directly. Marking iterates down cdr/env chains and only recurses on
 * car/params/body, so a million-element list marks without deep recursion.
 */

static void arena_init(void)
{
    for (int i = 0; i < ARENA_OBJECTS - 1; i++) free_next[i] = i + 1;
    free_next[ARENA_OBJECTS - 1] = -1;
    free_head = 0;
}

static int in_arena(value v)
{
    if (!IS_OBJ(v)) return 0;
    obj_t *o = OBJ(v);
    if (o < arena || o >= arena + ARENA_OBJECTS) return 0;
    return ((char *)o - (char *)arena) % (long)sizeof(obj_t) == 0;
}

static void mark(value v)
{
mark_loop:
    if (!in_arena(v)) return;
    int i = (int)(OBJ(v) - arena);
    if (marks[i]) return;
    marks[i] = 1;
    obj_t *o = &arena[i];
    switch (o->type) {
    case T_PAIR:
        mark(o->as.pair.car);
        v = o->as.pair.cdr;
        goto mark_loop;
    case T_CLOSURE:
        mark(o->as.closure.params);
        mark(o->as.closure.body);
        v = o->as.closure.env;
        goto mark_loop;
    default:
        return;
    }
}

static void mark_range(char *lo, char *hi)
{
    if (lo > hi) {
        char *tmp = lo;
        lo = hi;
        hi = tmp;
    }
    /* Align the low end up to a value boundary. */
    while (((uintptr_t)lo % sizeof(value)) != 0) lo++;
    for (char *p = lo; p + sizeof(value) <= hi; p += sizeof(value)) {
        mark(*(value *)p);
    }
}

static void gc(void)
{
    jmp_buf registers;
    (void)setjmp(registers); /* spill callee-saved registers to the stack */

    for (int i = 0; i < ARENA_OBJECTS; i++) marks[i] = 0;

    /* Precise roots. */
    mark(global_env);
    for (unsigned int i = 0; i < symbol_count; i++) mark(symbols[i]);

    /* Conservative roots: spilled registers + the live C stack. */
    char probe;
    mark_range((char *)registers, (char *)registers + sizeof(registers));
    mark_range(&probe, stack_bottom);

    /* Sweep: rebuild the free list from every unmarked slot. */
    free_head = -1;
    for (int i = ARENA_OBJECTS - 1; i >= 0; i--) {
        if (marks[i]) continue;
        free_next[i] = free_head;
        free_head = i;
    }
    gc_cycles++;
}

static obj_t *allocate(obj_type type)
{
    if (free_head == -1) {
        gc();
        if (free_head == -1) scm_error("out of object memory");
    }
    int i = free_head;
    free_head = free_next[i];
    obj_t *o = &arena[i];
    o->type = type;
    return o;
}

/* ------------------------------------------------------------------ */
/* Freestanding string helpers                                         */

static int str_length(const char *s)
{
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int str_equal_n(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static const char *pool_copy(const char *chars, int length)
{
    if (string_pool_top + (unsigned)length + 1 > STRING_POOL_BYTES) {
        scm_error("out of string memory");
    }
    char *dst = &string_pool[string_pool_top];
    for (int i = 0; i < length; i++) dst[i] = chars[i];
    dst[length] = '\0';
    string_pool_top += (unsigned)length + 1;
    return dst;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */

static void default_output(const char *text)
{
    (void)text;
}

static void emit(const char *text)
{
    output(text);
}

static void emit_int(intptr_t value)
{
    char digits[24];
    int i = 0;
    int negative = value < 0;
    uintptr_t magnitude = negative ? (uintptr_t)(-value) : (uintptr_t)value;
    if (magnitude == 0) digits[i++] = '0';
    while (magnitude != 0) {
        digits[i++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    char buffer[26];
    int j = 0;
    if (negative) buffer[j++] = '-';
    while (i != 0) buffer[j++] = digits[--i];
    buffer[j] = '\0';
    emit(buffer);
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */

static value cons(value car, value cdr)
{
    obj_t *o = allocate(T_PAIR);
    o->as.pair.car = car;
    o->as.pair.cdr = cdr;
    return (value)o;
}

static value car(value v)
{
    if (!IS_OBJ(v) || TYPE(v) != T_PAIR) scm_error("car: not a pair");
    return OBJ(v)->as.pair.car;
}

static value cdr(value v)
{
    if (!IS_OBJ(v) || TYPE(v) != T_PAIR) scm_error("cdr: not a pair");
    return OBJ(v)->as.pair.cdr;
}

static value cadr(value v) { return car(cdr(v)); }
static value caddr(value v) { return car(cdr(cdr(v))); }
static value cddr(value v) { return cdr(cdr(v)); }

static value intern(const char *chars, int length)
{
    for (unsigned int i = 0; i < symbol_count; i++) {
        const char *name = OBJ(symbols[i])->as.symbol.name;
        if (str_length(name) == length && str_equal_n(name, chars, length)) {
            return symbols[i];
        }
    }
    if (symbol_count == SYMBOL_MAX) scm_error("too many symbols");
    obj_t *o = allocate(T_SYMBOL);
    o->as.symbol.name = pool_copy(chars, length);
    value v = (value)o;
    symbols[symbol_count++] = v;
    return v;
}

static value make_string(const char *chars, int length)
{
    obj_t *o = allocate(T_STRING);
    o->as.string.chars = pool_copy(chars, length);
    o->as.string.length = length;
    return (value)o;
}

static value make_primitive(primitive_fn fn, const char *name)
{
    obj_t *o = allocate(T_PRIMITIVE);
    o->as.primitive.fn = fn;
    o->as.primitive.name = name;
    return (value)o;
}

static value make_closure(value params, value body, value env)
{
    obj_t *o = allocate(T_CLOSURE);
    o->as.closure.params = params;
    o->as.closure.body = body;
    o->as.closure.env = env;
    return (value)o;
}

/* Interned special-form symbols, filled in by scm_init. */
static value sym_quote, sym_if, sym_define, sym_lambda, sym_let, sym_begin,
    sym_set, sym_cond, sym_else, sym_and, sym_or;

/* ------------------------------------------------------------------ */
/* Reader                                                              */

static const char *read_ptr;

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int is_delimiter(char c)
{
    return c == '\0' || is_space(c) || c == '(' || c == ')' || c == '"' ||
           c == ';';
}

static void skip_whitespace(void)
{
    for (;;) {
        while (is_space(*read_ptr)) read_ptr++;
        if (*read_ptr == ';') {
            while (*read_ptr != '\0' && *read_ptr != '\n') read_ptr++;
            continue;
        }
        return;
    }
}

static value read_expr(void);

static value read_list(void)
{
    skip_whitespace();
    if (*read_ptr == ')') {
        read_ptr++;
        return NIL;
    }
    if (*read_ptr == '\0') scm_error("unexpected end of input in list");
    if (*read_ptr == '.' && is_delimiter(read_ptr[1])) {
        read_ptr++;
        value tail = read_expr();
        skip_whitespace();
        if (*read_ptr != ')') scm_error("expected ) after dotted tail");
        read_ptr++;
        return tail;
    }
    value head = read_expr();
    value rest = read_list();
    return cons(head, rest);
}

static int parse_int(const char *start, int length, intptr_t *out)
{
    int i = 0;
    int negative = 0;
    if (length == 0) return 0;
    if (start[0] == '-' || start[0] == '+') {
        negative = start[0] == '-';
        i = 1;
        if (i == length) return 0; /* a lone "-" or "+" is a symbol */
    }
    intptr_t value = 0;
    for (; i < length; i++) {
        if (start[i] < '0' || start[i] > '9') return 0;
        value = value * 10 + (start[i] - '0');
    }
    *out = negative ? -value : value;
    return 1;
}

static value read_atom(void)
{
    const char *start = read_ptr;
    while (!is_delimiter(*read_ptr)) read_ptr++;
    int length = (int)(read_ptr - start);
    intptr_t number;
    if (parse_int(start, length, &number)) {
        return MAKE_FIXNUM(number);
    }
    return intern(start, length);
}

static value read_string(void)
{
    read_ptr++; /* opening quote */
    const char *start = read_ptr;
    while (*read_ptr != '"' && *read_ptr != '\0') read_ptr++;
    if (*read_ptr != '"') scm_error("unterminated string");
    int length = (int)(read_ptr - start);
    value s = make_string(start, length);
    read_ptr++; /* closing quote */
    return s;
}

static value read_expr(void)
{
    skip_whitespace();
    char c = *read_ptr;
    if (c == '\0') return EOF_VAL;
    if (c == '(') {
        read_ptr++;
        return read_list();
    }
    if (c == ')') scm_error("unexpected )");
    if (c == '"') return read_string();
    if (c == '\'') {
        read_ptr++;
        return cons(sym_quote, cons(read_expr(), NIL));
    }
    if (c == '#') {
        if (read_ptr[1] == 't') {
            read_ptr += 2;
            return TRUE;
        }
        if (read_ptr[1] == 'f') {
            read_ptr += 2;
            return FALSE;
        }
        scm_error("unknown # syntax");
    }
    return read_atom();
}

/* ------------------------------------------------------------------ */
/* Printer                                                             */

static void write_value(value v, int display);

static void write_list(value v, int display)
{
    emit("(");
    for (;;) {
        write_value(car(v), display);
        value rest = cdr(v);
        if (rest == NIL) break;
        if (!IS_OBJ(rest) || TYPE(rest) != T_PAIR) {
            emit(" . ");
            write_value(rest, display);
            break;
        }
        emit(" ");
        v = rest;
    }
    emit(")");
}

static void write_value(value v, int display)
{
    if (IS_FIXNUM(v)) {
        emit_int(FIXNUM_VALUE(v));
        return;
    }
    switch (TYPE(v)) {
    case T_NIL:
        emit("()");
        return;
    case T_BOOL:
        emit(v == TRUE ? "#t" : "#f");
        return;
    case T_SYMBOL:
        emit(OBJ(v)->as.symbol.name);
        return;
    case T_STRING:
        if (display) {
            emit(OBJ(v)->as.string.chars);
        } else {
            emit("\"");
            emit(OBJ(v)->as.string.chars);
            emit("\"");
        }
        return;
    case T_PAIR:
        write_list(v, display);
        return;
    case T_PRIMITIVE:
        emit("#<primitive ");
        emit(OBJ(v)->as.primitive.name);
        emit(">");
        return;
    case T_CLOSURE:
        emit("#<closure>");
        return;
    case T_EOF:
        emit("#<eof>");
        return;
    case T_UNSPEC:
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Environments: frame chain. env = (frame . parent), frame = alist.   */

static value make_env(value parent)
{
    return cons(NIL, parent);
}

static void env_define(value env, value symbol, value val)
{
    value frame = car(env);
    OBJ(env)->as.pair.car = cons(cons(symbol, val), frame);
}

static value *env_slot(value env, value symbol)
{
    while (env != NIL) {
        value frame = car(env);
        while (frame != NIL) {
            value binding = car(frame);
            if (car(binding) == symbol) {
                return &OBJ(binding)->as.pair.cdr;
            }
            frame = cdr(frame);
        }
        env = cdr(env);
    }
    return NULL;
}

static value env_lookup(value env, value symbol)
{
    value *slot = env_slot(env, symbol);
    if (slot == NULL) {
        emit("unbound variable: ");
        emit(OBJ(symbol)->as.symbol.name);
        emit("\n");
        scm_error("unbound variable");
    }
    return *slot;
}

static void env_set(value env, value symbol, value val)
{
    value *slot = env_slot(env, symbol);
    if (slot == NULL) scm_error("set!: unbound variable");
    *slot = val;
}

/* Bind a lambda's params to args in a fresh frame over `closure_env`.
 * A symbol param list binds all args as a list (variadic). */
static value bind_params(value params, value args, value closure_env)
{
    value env = make_env(closure_env);
    while (IS_OBJ(params) && TYPE(params) == T_PAIR) {
        if (args == NIL) scm_error("too few arguments");
        env_define(env, car(params), car(args));
        params = cdr(params);
        args = cdr(args);
    }
    if (params != NIL) {
        env_define(env, params, args); /* variadic rest */
    } else if (args != NIL) {
        scm_error("too many arguments");
    }
    return env;
}

/* ------------------------------------------------------------------ */
/* Evaluator (tail-call trampoline)                                    */

#define IS_TRUE(v) ((v) != FALSE)

static value eval(value expr, value env);

static value eval_args(value list, value env)
{
    if (list == NIL) return NIL;
    value first = eval(car(list), env);
    return cons(first, eval_args(cdr(list), env));
}

static value eval(value expr, value env)
{
tail:
    if (IS_FIXNUM(expr)) return expr;
    switch (TYPE(expr)) {
    case T_SYMBOL:
        return env_lookup(env, expr);
    case T_PAIR:
        break;
    default:
        return expr; /* strings, booleans, nil, procedures self-evaluate */
    }

    value op = car(expr);

    if (op == sym_quote) {
        return cadr(expr);
    }
    if (op == sym_if) {
        value test = eval(cadr(expr), env);
        if (IS_TRUE(test)) {
            expr = caddr(expr);
        } else {
            value else_branch = cddr(expr);
            if (cdr(else_branch) == NIL) return UNSPEC;
            expr = car(cdr(else_branch));
        }
        goto tail;
    }
    if (op == sym_define) {
        value target = cadr(expr);
        if (IS_OBJ(target) && TYPE(target) == T_PAIR) {
            /* (define (f a b) body...) sugar */
            value name = car(target);
            value lambda = make_closure(cdr(target), cddr(expr), env);
            env_define(env, name, lambda);
        } else {
            value val = cddr(expr) == NIL ? UNSPEC : eval(caddr(expr), env);
            env_define(env, target, val);
        }
        return UNSPEC;
    }
    if (op == sym_lambda) {
        return make_closure(cadr(expr), cddr(expr), env);
    }
    if (op == sym_set) {
        env_set(env, cadr(expr), eval(caddr(expr), env));
        return UNSPEC;
    }
    if (op == sym_begin) {
        value body = cdr(expr);
        if (body == NIL) return UNSPEC;
        while (cdr(body) != NIL) {
            eval(car(body), env);
            body = cdr(body);
        }
        expr = car(body);
        goto tail;
    }
    if (op == sym_let) {
        value bindings = cadr(expr);
        value new_env = make_env(env);
        while (bindings != NIL) {
            value binding = car(bindings);
            env_define(new_env, car(binding), eval(cadr(binding), env));
            bindings = cdr(bindings);
        }
        value body = cddr(expr);
        if (body == NIL) return UNSPEC;
        while (cdr(body) != NIL) {
            eval(car(body), new_env);
            body = cdr(body);
        }
        expr = car(body);
        env = new_env;
        goto tail;
    }
    if (op == sym_cond) {
        value clauses = cdr(expr);
        while (clauses != NIL) {
            value clause = car(clauses);
            if (car(clause) == sym_else || IS_TRUE(eval(car(clause), env))) {
                value body = cdr(clause);
                if (body == NIL) return TRUE;
                while (cdr(body) != NIL) {
                    eval(car(body), env);
                    body = cdr(body);
                }
                expr = car(body);
                goto tail;
            }
            clauses = cdr(clauses);
        }
        return UNSPEC;
    }
    if (op == sym_and) {
        value rest = cdr(expr);
        if (rest == NIL) return TRUE;
        while (cdr(rest) != NIL) {
            if (!IS_TRUE(eval(car(rest), env))) return FALSE;
            rest = cdr(rest);
        }
        expr = car(rest);
        goto tail;
    }
    if (op == sym_or) {
        value rest = cdr(expr);
        if (rest == NIL) return FALSE;
        while (cdr(rest) != NIL) {
            value v = eval(car(rest), env);
            if (IS_TRUE(v)) return v;
            rest = cdr(rest);
        }
        expr = car(rest);
        goto tail;
    }

    /* Application. */
    value fn = eval(op, env);
    value args = eval_args(cdr(expr), env);
    if (IS_OBJ(fn) && TYPE(fn) == T_PRIMITIVE) {
        return OBJ(fn)->as.primitive.fn(args);
    }
    if (IS_OBJ(fn) && TYPE(fn) == T_CLOSURE) {
        env = bind_params(OBJ(fn)->as.closure.params, args,
                          OBJ(fn)->as.closure.env);
        value body = OBJ(fn)->as.closure.body;
        if (body == NIL) return UNSPEC;
        while (cdr(body) != NIL) {
            eval(car(body), env);
            body = cdr(body);
        }
        expr = car(body); /* proper tail call */
        goto tail;
    }
    scm_error("not applicable");
    return UNSPEC;
}

/* ------------------------------------------------------------------ */
/* Primitives                                                          */

static intptr_t as_int(value v)
{
    if (!IS_FIXNUM(v)) scm_error("expected an integer");
    return FIXNUM_VALUE(v);
}

static value prim_add(value args)
{
    intptr_t sum = 0;
    while (args != NIL) {
        sum += as_int(car(args));
        args = cdr(args);
    }
    return MAKE_FIXNUM(sum);
}

static value prim_sub(value args)
{
    if (args == NIL) return MAKE_FIXNUM(0);
    intptr_t acc = as_int(car(args));
    args = cdr(args);
    if (args == NIL) return MAKE_FIXNUM(-acc);
    while (args != NIL) {
        acc -= as_int(car(args));
        args = cdr(args);
    }
    return MAKE_FIXNUM(acc);
}

static value prim_mul(value args)
{
    intptr_t product = 1;
    while (args != NIL) {
        product *= as_int(car(args));
        args = cdr(args);
    }
    return MAKE_FIXNUM(product);
}

static value prim_div(value args)
{
    intptr_t acc = as_int(car(args));
    args = cdr(args);
    while (args != NIL) {
        intptr_t d = as_int(car(args));
        if (d == 0) scm_error("division by zero");
        acc /= d;
        args = cdr(args);
    }
    return MAKE_FIXNUM(acc);
}

static value prim_mod(value args)
{
    intptr_t a = as_int(car(args));
    intptr_t b = as_int(cadr(args));
    if (b == 0) scm_error("division by zero");
    return MAKE_FIXNUM(a % b);
}

#define COMPARE_PRIM(name, op)                            \
    static value name(value args)                         \
    {                                                     \
        while (cdr(args) != NIL) {                        \
            if (!(as_int(car(args)) op as_int(cadr(args)))) \
                return FALSE;                             \
            args = cdr(args);                             \
        }                                                 \
        return TRUE;                                      \
    }
COMPARE_PRIM(prim_num_eq, ==)
COMPARE_PRIM(prim_lt, <)
COMPARE_PRIM(prim_gt, >)
COMPARE_PRIM(prim_le, <=)
COMPARE_PRIM(prim_ge, >=)

static value prim_cons(value args) { return cons(car(args), cadr(args)); }
static value prim_car(value args) { return car(car(args)); }
static value prim_cdr(value args) { return cdr(car(args)); }
static value prim_list(value args) { return args; }
static value prim_null(value args) { return car(args) == NIL ? TRUE : FALSE; }

static value prim_pair(value args)
{
    value v = car(args);
    return (IS_OBJ(v) && TYPE(v) == T_PAIR) ? TRUE : FALSE;
}

static value prim_eq(value args)
{
    return car(args) == cadr(args) ? TRUE : FALSE;
}

static value prim_not(value args)
{
    return car(args) == FALSE ? TRUE : FALSE;
}

static value prim_zero(value args)
{
    return as_int(car(args)) == 0 ? TRUE : FALSE;
}

static value prim_display(value args)
{
    write_value(car(args), 1);
    return UNSPEC;
}

static value prim_newline(value args)
{
    (void)args;
    emit("\n");
    return UNSPEC;
}

static value prim_write(value args)
{
    write_value(car(args), 0);
    return UNSPEC;
}

/* Desktop primitives. In the kernel these call c26_draw_* / c26_audio_* /
 * the compositor; in the prototype they emit a textual trace through the
 * sink so the wiring is visible and testable. This is the whole point:
 * every machine capability is one row in the primitive table. */
static void trace_call(const char *name, value args)
{
    emit("[");
    emit(name);
    while (args != NIL) {
        emit(" ");
        write_value(car(args), 1);
        args = cdr(args);
    }
    emit("]\n");
}

static value prim_cls(value args)
{
    trace_call("cls", args);
    return UNSPEC;
}
static value prim_color(value args)
{
    trace_call("color", args);
    return UNSPEC;
}
static value prim_plot(value args)
{
    trace_call("plot", args);
    return UNSPEC;
}
static value prim_line(value args)
{
    trace_call("line", args);
    return UNSPEC;
}
static value prim_rect(value args)
{
    trace_call("rect", args);
    return UNSPEC;
}
static value prim_text(value args)
{
    trace_call("text", args);
    return UNSPEC;
}
static value prim_sound(value args)
{
    trace_call("sound", args);
    return UNSPEC;
}

/* (range lo hi) -> (lo lo+1 ... hi-1), handy for the demos. */
static value prim_range(value args)
{
    intptr_t lo = as_int(car(args));
    intptr_t hi = as_int(cadr(args));
    value result = NIL;
    for (intptr_t i = hi - 1; i >= lo; i--) {
        result = cons(MAKE_FIXNUM(i), result);
    }
    return result;
}

static void define_primitive(const char *name, primitive_fn fn)
{
    value symbol = intern(name, str_length(name));
    env_define(global_env, symbol, make_primitive(fn, name));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */

void scm_set_output(void (*sink)(const char *))
{
    output = sink;
}

void scm_reset(void)
{
    scm_init();
}

void scm_init(void)
{
    if (output == NULL) output = default_output;
    arena_init();
    string_pool_top = 0;
    symbol_count = 0;
    gc_cycles = 0;
    /* A default stack anchor so an allocation before the first
       scm_eval_string still has a valid (if conservative) scan range. */
    char anchor;
    stack_bottom = &anchor;

    sym_quote = intern("quote", 5);
    sym_if = intern("if", 2);
    sym_define = intern("define", 6);
    sym_lambda = intern("lambda", 6);
    sym_let = intern("let", 3);
    sym_begin = intern("begin", 5);
    sym_set = intern("set!", 4);
    sym_cond = intern("cond", 4);
    sym_else = intern("else", 4);
    sym_and = intern("and", 3);
    sym_or = intern("or", 2);

    global_env = make_env(NIL);

    define_primitive("+", prim_add);
    define_primitive("-", prim_sub);
    define_primitive("*", prim_mul);
    define_primitive("/", prim_div);
    define_primitive("modulo", prim_mod);
    define_primitive("=", prim_num_eq);
    define_primitive("<", prim_lt);
    define_primitive(">", prim_gt);
    define_primitive("<=", prim_le);
    define_primitive(">=", prim_ge);
    define_primitive("cons", prim_cons);
    define_primitive("car", prim_car);
    define_primitive("cdr", prim_cdr);
    define_primitive("list", prim_list);
    define_primitive("null?", prim_null);
    define_primitive("pair?", prim_pair);
    define_primitive("eq?", prim_eq);
    define_primitive("not", prim_not);
    define_primitive("zero?", prim_zero);
    define_primitive("display", prim_display);
    define_primitive("write", prim_write);
    define_primitive("newline", prim_newline);
    define_primitive("range", prim_range);
    /* desktop-as-primitives */
    define_primitive("cls", prim_cls);
    define_primitive("color", prim_color);
    define_primitive("plot", prim_plot);
    define_primitive("line", prim_line);
    define_primitive("rect", prim_rect);
    define_primitive("text", prim_text);
    define_primitive("sound", prim_sound);
}

int scm_eval_string(const char *src, int echo)
{
    /* Anchor the conservative stack scan at this outermost eval frame, so a
       GC triggered deep in eval() scans every live temporary above it. */
    char anchor;
    stack_bottom = &anchor;

    if (setjmp(error_jump)) {
        emit("ERROR: ");
        emit(error_message);
        emit("\n");
        return 0;
    }
    read_ptr = src;
    for (;;) {
        value form = read_expr();
        if (form == EOF_VAL) break;
        value result = eval(form, global_env);
        if (echo && result != UNSPEC) {
            write_value(result, 0);
            emit("\n");
        }
    }
    return 1;
}
