/* TINYC: the self-hosting endgame. A tiny C compiler that runs ON the
 * machine. It reads a C-subset source file from C26FS, compiles it to native
 * RV64 machine code with a single backpatching pass, and writes a runnable
 * cartridge back to disk — where RUN executes it as a protected process.
 *
 * The language is an implicit main() body: a sequence of statements.
 *   int NAME;            int NAME = EXPR;      NAME = EXPR;
 *   print(EXPR);         if (EXPR) { ... } else { ... }
 *   while (EXPR) { ... } return EXPR;   return;
 * Expressions: integer literals, variables, + - * / (M-extension mul/div),
 * comparisons < > == != <= >=, parentheses and unary minus. Precedence is
 * * / above + - above comparisons, left-associative; comparisons yield 0/1.
 * Comments // ... and (asterisk-slash) blocks and whitespace are ignored.
 *
 * Codegen is a stack machine: every expression leaves its result in a0, using
 * the machine stack to hold the left operand while the right is computed.
 * Variables live in a fixed frame addressed from s1; the api pointer is kept
 * in s0. Type "SOURCE DEST" and press Enter; Q quits. */

#include "ui.h"

#define SOURCE_MAX 8000
#define OUTPUT_MAX 8000
#define MAX_TOK 4000
#define MAX_NAME 16
#define MAX_VARS 32
#define CART_HEADER 32

/* RV64 register numbers we generate against. */
#define R_X0 0
#define R_RA 1
#define R_SP 2
#define R_T0 5
#define R_S0 8   /* api pointer */
#define R_S1 9   /* variable frame base */
#define R_A0 10
#define R_A1 11

/* Frame: [0(sp)=saved ra][16 + i*8 = variable slot i]. FRAME is a multiple of
 * 16 so sp stays 16-byte aligned across api calls. */
#define VAR_BASE 16
#define FRAME (VAR_BASE + MAX_VARS * 8)

static c26_ui_t ui;
static const c26_api_t *g_api;

static char source[SOURCE_MAX + 1];
static uint8_t output[OUTPUT_MAX];
static uint32_t cursor;          /* bytes emitted, cart header included */

static char status_line[64];
static char error_line[64];
static const char *compile_error; /* non-null once compilation has failed */

/* ------------------------------------------------------------------ */
/* Tokens                                                              */

enum {
    K_EOF, K_NUM, K_ID,
    K_INT, K_IF, K_ELSE, K_WHILE, K_RETURN, K_PRINT,
    K_ASSIGN, K_EQ, K_NE, K_LT, K_GT, K_LE, K_GE,
    K_PLUS, K_MINUS, K_STAR, K_SLASH,
    K_LP, K_RP, K_LB, K_RB, K_SEMI
};

static int tok_kind[MAX_TOK];
static int64_t tok_num[MAX_TOK];
static char tok_name[MAX_TOK][MAX_NAME];
static int ntok;
static int tp;                   /* current token index during parsing */

/* Variable table: name -> frame slot. */
static char var_names[MAX_VARS][MAX_NAME];
static int nvars;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */

static int str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static void fail(const char *why)
{
    if (compile_error == 0) compile_error = why;
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* ------------------------------------------------------------------ */
/* Lexer: turn the source into a flat token stream                     */

static void add_token(int kind)
{
    if (ntok >= MAX_TOK) { fail("TOO MANY TOKENS"); return; }
    tok_kind[ntok] = kind;
    tok_num[ntok] = 0;
    tok_name[ntok][0] = '\0';
    ntok++;
}

static void tokenize(void)
{
    ntok = 0;
    const char *p = source;
    while (*p != '\0' && compile_error == 0) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        if (c == '/' && p[1] == '/') {          /* line comment */
            p += 2;
            while (*p != '\0' && *p != '\n') p++;
            continue;
        }
        if (c == '/' && p[1] == '*') {          /* block comment */
            p += 2;
            while (*p != '\0' && !(*p == '*' && p[1] == '/')) p++;
            if (*p != '\0') p += 2;
            continue;
        }
        if (is_digit(c)) {                       /* integer literal */
            int64_t value = 0;
            while (is_digit(*p)) { value = value * 10 + (*p - '0'); p++; }
            add_token(K_NUM);
            tok_num[ntok - 1] = value;
            continue;
        }
        if (is_alpha(c)) {                        /* identifier or keyword */
            char name[MAX_NAME];
            int i = 0;
            while ((is_alpha(*p) || is_digit(*p)) && i < MAX_NAME - 1) {
                name[i++] = *p++;
            }
            name[i] = '\0';
            while (is_alpha(*p) || is_digit(*p)) p++; /* skip overlong tail */
            int kind = K_ID;
            if (str_eq(name, "int")) kind = K_INT;
            else if (str_eq(name, "if")) kind = K_IF;
            else if (str_eq(name, "else")) kind = K_ELSE;
            else if (str_eq(name, "while")) kind = K_WHILE;
            else if (str_eq(name, "return")) kind = K_RETURN;
            else if (str_eq(name, "print")) kind = K_PRINT;
            add_token(kind);
            if (kind == K_ID) {
                char *d = tok_name[ntok - 1];
                for (int j = 0; name[j] != '\0'; j++) d[j] = name[j];
                d[i] = '\0';
            }
            continue;
        }
        /* Operators and punctuation, two-character forms first. */
        if (c == '=' && p[1] == '=') { add_token(K_EQ); p += 2; continue; }
        if (c == '!' && p[1] == '=') { add_token(K_NE); p += 2; continue; }
        if (c == '<' && p[1] == '=') { add_token(K_LE); p += 2; continue; }
        if (c == '>' && p[1] == '=') { add_token(K_GE); p += 2; continue; }
        switch (c) {
        case '=': add_token(K_ASSIGN); break;
        case '<': add_token(K_LT); break;
        case '>': add_token(K_GT); break;
        case '+': add_token(K_PLUS); break;
        case '-': add_token(K_MINUS); break;
        case '*': add_token(K_STAR); break;
        case '/': add_token(K_SLASH); break;
        case '(': add_token(K_LP); break;
        case ')': add_token(K_RP); break;
        case '{': add_token(K_LB); break;
        case '}': add_token(K_RB); break;
        case ';': add_token(K_SEMI); break;
        default: fail("BAD CHARACTER"); break;
        }
        p++;
    }
    add_token(K_EOF);
}

/* ------------------------------------------------------------------ */
/* Emission and instruction encoders (same forms as the ASM cartridge) */

static void emit8(uint8_t byte)
{
    if (cursor < OUTPUT_MAX) output[cursor] = byte;
    else fail("PROGRAM TOO LARGE");
    cursor++;
}

static uint32_t enc_i(int op, int f3, int rd, int rs1, int64_t imm)
{
    return ((uint32_t)(imm & 0xfff) << 20) | ((uint32_t)rs1 << 15) |
           ((uint32_t)f3 << 12) | ((uint32_t)rd << 7) | (uint32_t)op;
}

static uint32_t enc_r(int f7, int rs2, int rs1, int f3, int rd, int op)
{
    return ((uint32_t)f7 << 25) | ((uint32_t)rs2 << 20) |
           ((uint32_t)rs1 << 15) | ((uint32_t)f3 << 12) |
           ((uint32_t)rd << 7) | (uint32_t)op;
}

static uint32_t enc_s(int f3, int rs2, int rs1, int64_t imm)
{
    return ((uint32_t)((imm >> 5) & 0x7f) << 25) | ((uint32_t)rs2 << 20) |
           ((uint32_t)rs1 << 15) | ((uint32_t)f3 << 12) |
           ((uint32_t)(imm & 0x1f) << 7) | 0x23U;
}

static uint32_t enc_b(int f3, int rs1, int rs2, int64_t delta)
{
    uint32_t imm = (uint32_t)delta;
    return (((imm >> 12) & 1) << 31) | (((imm >> 5) & 0x3f) << 25) |
           ((uint32_t)rs2 << 20) | ((uint32_t)rs1 << 15) |
           ((uint32_t)f3 << 12) | (((imm >> 1) & 0xf) << 8) |
           (((imm >> 11) & 1) << 7) | 0x63U;
}

static uint32_t enc_j(int rd, int64_t delta)
{
    uint32_t imm = (uint32_t)delta;
    return (((imm >> 20) & 1) << 31) | (((imm >> 1) & 0x3ff) << 21) |
           (((imm >> 11) & 1) << 20) | (((imm >> 12) & 0xff) << 12) |
           ((uint32_t)rd << 7) | 0x6fU;
}

static uint32_t enc_u(int op, int rd, int64_t imm20)
{
    return ((uint32_t)(imm20 & 0xfffff) << 12) | ((uint32_t)rd << 7) |
           (uint32_t)op;
}

static void emit32(uint32_t word)
{
    emit8((uint8_t)word);
    emit8((uint8_t)(word >> 8));
    emit8((uint8_t)(word >> 16));
    emit8((uint8_t)(word >> 24));
}

/* Overwrite a previously emitted 32-bit word (for branch backpatching). */
static void put32(uint32_t at, uint32_t word)
{
    if (at + 4 > OUTPUT_MAX) return;
    output[at] = (uint8_t)word;
    output[at + 1] = (uint8_t)(word >> 8);
    output[at + 2] = (uint8_t)(word >> 16);
    output[at + 3] = (uint8_t)(word >> 24);
}

/* Load a 64-bit immediate into rd (lui/addiw for large values). */
static void emit_li(int rd, int64_t imm)
{
    if (imm >= -2048 && imm < 2048) {
        emit32(enc_i(0x13, 0, rd, R_X0, imm));       /* addi rd, x0, imm */
        return;
    }
    int64_t hi = (imm + 0x800) >> 12;
    int64_t lo = imm - (hi << 12);
    emit32(enc_u(0x37, rd, hi));                      /* lui   rd, hi */
    emit32(enc_i(0x1b, 0, rd, rd, lo));               /* addiw rd, rd, lo */
}

static void emit_mv(int rd, int rs) { emit32(enc_i(0x13, 0, rd, rs, 0)); }

static void emit_push_a0(void)
{
    emit32(enc_i(0x13, 0, R_SP, R_SP, -8));           /* addi sp, sp, -8 */
    emit32(enc_s(3, R_A0, R_SP, 0));                  /* sd   a0, 0(sp)  */
}

/* Pop the previously pushed left operand into a1, leaving a0 as the right. */
static void emit_pop_a1_swap(void)
{
    emit_mv(R_A1, R_A0);                              /* a1 = right */
    emit32(enc_i(0x03, 3, R_A0, R_SP, 0));            /* ld a0, 0(sp) = left */
    emit32(enc_i(0x13, 0, R_SP, R_SP, 8));            /* addi sp, sp, 8 */
}

/* ------------------------------------------------------------------ */
/* Variables                                                           */

static int var_slot(const char *name)
{
    for (int i = 0; i < nvars; i++) {
        if (str_eq(var_names[i], name)) return i;
    }
    return -1;
}

static int declare_var(const char *name)
{
    if (var_slot(name) >= 0) { fail("DUPLICATE VARIABLE"); return 0; }
    if (nvars >= MAX_VARS) { fail("TOO MANY VARIABLES"); return 0; }
    char *d = var_names[nvars];
    int i = 0;
    while (name[i] != '\0' && i < MAX_NAME - 1) { d[i] = name[i]; i++; }
    d[i] = '\0';
    return nvars++;
}

static int var_offset(int slot) { return VAR_BASE + slot * 8; }

static void emit_load_var(int slot)   /* a0 = var[slot] */
{
    emit32(enc_i(0x03, 3, R_A0, R_S1, var_offset(slot)));
}

static void emit_store_var(int slot)  /* var[slot] = a0 */
{
    emit32(enc_s(3, R_A0, R_S1, var_offset(slot)));
}

/* ------------------------------------------------------------------ */
/* Parser + code generator (recursive descent, result always in a0)    */

static void gen_comparison(void);

static int cur(void) { return tok_kind[tp]; }

static void advance(void) { if (tok_kind[tp] != K_EOF) tp++; }

static int accept(int kind)
{
    if (cur() == kind) { advance(); return 1; }
    return 0;
}

static void expect(int kind, const char *why)
{
    if (!accept(kind)) fail(why);
}

/* primary := NUM | ID | '(' comparison ')' | '-' primary */
static void gen_primary(void)
{
    if (compile_error) return;
    if (accept(K_MINUS)) {                            /* unary minus */
        gen_primary();
        emit32(enc_r(0x20, R_A0, R_X0, 0, R_A0, 0x33)); /* sub a0, x0, a0 */
        return;
    }
    if (cur() == K_NUM) {
        emit_li(R_A0, tok_num[tp]);
        advance();
        return;
    }
    if (cur() == K_ID) {
        int slot = var_slot(tok_name[tp]);
        if (slot < 0) { fail("UNKNOWN VARIABLE"); return; }
        emit_load_var(slot);
        advance();
        return;
    }
    if (accept(K_LP)) {
        gen_comparison();
        expect(K_RP, "EXPECTED )");
        return;
    }
    fail("EXPECTED EXPRESSION");
}

/* term := primary (('*' | '/') primary)* */
static void gen_term(void)
{
    gen_primary();
    while (cur() == K_STAR || cur() == K_SLASH) {
        int op = cur();
        advance();
        emit_push_a0();
        gen_primary();
        emit_pop_a1_swap();                            /* a0=left, a1=right */
        if (op == K_STAR)
            emit32(enc_r(1, R_A1, R_A0, 0, R_A0, 0x33)); /* mul a0, a0, a1 */
        else
            emit32(enc_r(1, R_A1, R_A0, 4, R_A0, 0x33)); /* div a0, a0, a1 */
    }
}

/* sum := term (('+' | '-') term)* */
static void gen_sum(void)
{
    gen_term();
    while (cur() == K_PLUS || cur() == K_MINUS) {
        int op = cur();
        advance();
        emit_push_a0();
        gen_term();
        emit_pop_a1_swap();
        if (op == K_PLUS)
            emit32(enc_r(0, R_A1, R_A0, 0, R_A0, 0x33)); /* add a0, a0, a1 */
        else
            emit32(enc_r(0x20, R_A1, R_A0, 0, R_A0, 0x33)); /* sub a0, a0, a1 */
    }
}

/* comparison := sum (relop sum)?  — comparisons yield 0/1 */
static void gen_comparison(void)
{
    gen_sum();
    while (cur() == K_LT || cur() == K_GT || cur() == K_EQ ||
           cur() == K_NE || cur() == K_LE || cur() == K_GE) {
        int op = cur();
        advance();
        emit_push_a0();
        gen_sum();
        emit_pop_a1_swap();                            /* a0=left, a1=right */
        switch (op) {
        case K_LT: /* a0 < a1 */
            emit32(enc_r(0, R_A1, R_A0, 2, R_A0, 0x33)); /* slt a0, a0, a1 */
            break;
        case K_GT: /* a0 > a1  ==  a1 < a0 */
            emit32(enc_r(0, R_A0, R_A1, 2, R_A0, 0x33)); /* slt a0, a1, a0 */
            break;
        case K_EQ: /* a0 == a1 */
            emit32(enc_r(0x20, R_A1, R_A0, 0, R_A0, 0x33)); /* sub a0,a0,a1 */
            emit32(enc_i(0x13, 3, R_A0, R_A0, 1));       /* sltiu a0, a0, 1 */
            break;
        case K_NE: /* a0 != a1 */
            emit32(enc_r(0x20, R_A1, R_A0, 0, R_A0, 0x33)); /* sub a0,a0,a1 */
            emit32(enc_r(0, R_A0, R_X0, 3, R_A0, 0x33)); /* sltu a0, x0, a0 */
            break;
        case K_LE: /* a0 <= a1  ==  !(a1 < a0) */
            emit32(enc_r(0, R_A0, R_A1, 2, R_A0, 0x33)); /* slt a0, a1, a0 */
            emit32(enc_i(0x13, 4, R_A0, R_A0, 1));       /* xori a0, a0, 1 */
            break;
        case K_GE: /* a0 >= a1  ==  !(a0 < a1) */
            emit32(enc_r(0, R_A1, R_A0, 2, R_A0, 0x33)); /* slt a0, a0, a1 */
            emit32(enc_i(0x13, 4, R_A0, R_A0, 1));       /* xori a0, a0, 1 */
            break;
        }
    }
}

/* An api call by field offset: t0 = off(s0); jalr ra, t0. */
static void emit_api_call(int field_offset)
{
    emit32(enc_i(0x03, 3, R_T0, R_S0, field_offset)); /* ld t0, off(s0) */
    emit32(enc_i(0x67, 0, R_RA, R_T0, 0));            /* jalr ra, 0(t0) */
}

/* print(EXPR): put_int at 24(s0), then putc('\n') at 16(s0). */
static void emit_print(void)
{
    emit_api_call(24);                                /* put_int(a0) */
    emit_li(R_A0, '\n');
    emit_api_call(16);                                /* putc('\n') */
}

/* Function epilogue: restore ra, drop the frame, return to the kernel. */
static void emit_epilogue(void)
{
    emit32(enc_i(0x03, 3, R_RA, R_SP, 0));            /* ld ra, 0(sp) */
    emit32(enc_i(0x13, 0, R_SP, R_SP, FRAME));        /* addi sp, sp, FRAME */
    emit32(enc_i(0x67, 0, R_X0, R_RA, 0));            /* ret */
}

static void gen_statement(void);

/* Parse '{' statements '}'. */
static void gen_block(void)
{
    expect(K_LB, "EXPECTED {");
    while (cur() != K_RB && cur() != K_EOF && compile_error == 0) {
        gen_statement();
    }
    expect(K_RB, "EXPECTED }");
}

static void gen_statement(void)
{
    if (compile_error) return;
    switch (cur()) {
    case K_INT: {
        advance();
        if (cur() != K_ID) { fail("EXPECTED NAME"); return; }
        int slot = declare_var(tok_name[tp]);
        advance();
        if (accept(K_ASSIGN)) {
            gen_comparison();
        } else {
            emit_li(R_A0, 0);                          /* default 0 */
        }
        emit_store_var(slot);
        expect(K_SEMI, "EXPECTED ;");
        return;
    }
    case K_ID: {
        int slot = var_slot(tok_name[tp]);
        if (slot < 0) { fail("UNKNOWN VARIABLE"); return; }
        advance();
        expect(K_ASSIGN, "EXPECTED =");
        gen_comparison();
        emit_store_var(slot);
        expect(K_SEMI, "EXPECTED ;");
        return;
    }
    case K_PRINT: {
        advance();
        expect(K_LP, "EXPECTED (");
        gen_comparison();
        expect(K_RP, "EXPECTED )");
        expect(K_SEMI, "EXPECTED ;");
        emit_print();
        return;
    }
    case K_IF: {
        advance();
        expect(K_LP, "EXPECTED (");
        gen_comparison();
        expect(K_RP, "EXPECTED )");
        /* beq a0, x0, <else/end>: skip the then-block when false. */
        uint32_t beq_at = cursor;
        emit32(enc_b(0, R_A0, R_X0, 0));
        gen_block();
        if (accept(K_ELSE)) {
            uint32_t jmp_at = cursor;                  /* skip else after then */
            emit32(enc_j(R_X0, 0));
            put32(beq_at, enc_b(0, R_A0, R_X0,
                                (int64_t)cursor - (int64_t)beq_at));
            gen_block();
            put32(jmp_at, enc_j(R_X0, (int64_t)cursor - (int64_t)jmp_at));
        } else {
            put32(beq_at, enc_b(0, R_A0, R_X0,
                                (int64_t)cursor - (int64_t)beq_at));
        }
        return;
    }
    case K_WHILE: {
        advance();
        expect(K_LP, "EXPECTED (");
        uint32_t cond_at = cursor;                     /* loop test target */
        gen_comparison();
        expect(K_RP, "EXPECTED )");
        uint32_t beq_at = cursor;                      /* exit when false */
        emit32(enc_b(0, R_A0, R_X0, 0));
        gen_block();
        emit32(enc_j(R_X0, (int64_t)cond_at - (int64_t)cursor)); /* loop back */
        put32(beq_at, enc_b(0, R_A0, R_X0,
                            (int64_t)cursor - (int64_t)beq_at));
        return;
    }
    case K_RETURN: {
        advance();
        if (cur() != K_SEMI) gen_comparison();
        else emit_li(R_A0, 0);
        expect(K_SEMI, "EXPECTED ;");
        emit_epilogue();
        return;
    }
    case K_LB:                                         /* bare block */
        gen_block();
        return;
    default:
        fail("BAD STATEMENT");
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Compilation driver                                                  */

static int compile(const char *src_name, const char *dst_name)
{
    size_t size = 0;
    if (!g_api->fs_load(src_name, source, SOURCE_MAX, &size)) {
        fail("CANNOT LOAD SOURCE");
        return 0;
    }
    source[size] = '\0';

    compile_error = 0;
    nvars = 0;
    cursor = CART_HEADER;

    tokenize();
    tp = 0;

    /* Prologue: keep the api pointer, reserve the variable frame, save ra. */
    emit_mv(R_S0, R_A0);                              /* s0 = api */
    emit32(enc_i(0x13, 0, R_SP, R_SP, -FRAME));       /* addi sp, sp, -FRAME */
    emit32(enc_s(3, R_RA, R_SP, 0));                  /* sd ra, 0(sp) */
    emit_mv(R_S1, R_SP);                              /* s1 = frame base */

    /* The program body is a sequence of statements up to EOF. */
    while (cur() != K_EOF && compile_error == 0) {
        gen_statement();
    }

    /* Implicit `return 0;` at the end of the body. */
    emit_li(R_A0, 0);
    emit_epilogue();

    if (compile_error != 0) return 0;
    if (cursor > OUTPUT_MAX) { fail("PROGRAM TOO LARGE"); return 0; }

    /* Cartridge header: magic, ABI version 2, load size, no bss, entry=32. */
    uint32_t header[8] = {C26_CART_MAGIC, 2U, cursor, 0U, CART_HEADER, 0U, 0U, 0U};
    for (int i = 0; i < 8; i++) {
        output[i * 4] = (uint8_t)header[i];
        output[i * 4 + 1] = (uint8_t)(header[i] >> 8);
        output[i * 4 + 2] = (uint8_t)(header[i] >> 16);
        output[i * 4 + 3] = (uint8_t)(header[i] >> 24);
    }
    if (!g_api->fs_save(dst_name, output, cursor)) {
        fail("CANNOT SAVE OUTPUT");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */

static void draw(void)
{
    ui_clear(&ui);
    ui_titlebar(&ui, "TINYC", "SRC DST + ENTER  Q QUIT");
    ui_text(&ui, 12, 44, "TINY C -> RV64 CARTRIDGE COMPILER", UI_TEXT);
    ui_text(&ui, 12, 72, "INPUT:", UI_TEXT);
    ui_text(&ui, 100, 72, status_line, UI_BRIGHT);
    if (error_line[0] != '\0') {
        ui_text(&ui, 12, 100, error_line, UI_WARN);
    }
    ui_status(&ui, "OUTPUT RUNS WITH: RUN DST", UI_TEXT);
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    g_api = api;
    ui_init(&ui, api);
    api->puts("TINYC CART ONLINE\n");
    int used = 0;
    draw();

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) return 0;
        if (key == 'Q' && used == 0) {
            api->puts("TINYC CART EXIT\n");
            return 0;
        }
        if (key == '\n') {
            status_line[used] = '\0';
            /* Split "SRC DST". */
            char src[16];
            char dst[16];
            int i = 0;
            int j = 0;
            while (status_line[i] != ' ' && status_line[i] != '\0' && i < 15) {
                src[i] = status_line[i];
                i++;
            }
            src[i] = '\0';
            while (status_line[i] == ' ') i++;
            while (status_line[i] != '\0' && j < 15) dst[j++] = status_line[i++];
            dst[j] = '\0';
            error_line[0] = '\0';
            compile_error = 0;
            if (src[0] != '\0' && dst[0] != '\0' && compile(src, dst)) {
                api->puts("TINYC COMPILED\n");
                api->puts("COMPILED ");
                api->puts(src);
                api->puts(" -> ");
                api->puts(dst);
                api->puts(" (");
                api->put_int((int64_t)cursor);
                api->puts(" BYTES)\n");
                const char *ok = "OK - RUN IT";
                int k = 0;
                while (ok[k] != '\0') { error_line[k] = ok[k]; k++; }
                error_line[k] = '\0';
            } else {
                const char *why = compile_error ? compile_error : "COMPILE FAILED";
                api->puts("TINYC ERROR: ");
                api->puts(why);
                api->putc('\n');
                int k = 0;
                while (why[k] != '\0' && k < 40) { error_line[k] = why[k]; k++; }
                error_line[k] = '\0';
            }
            used = 0;
            status_line[0] = '\0';
            draw();
        } else if (key == '\b' || key == 0x7f) {
            if (used > 0) {
                used--;
                status_line[used] = '\0';
                draw();
            }
        } else if (key >= 32 && key <= 126 && used < 40) {
            status_line[used++] = (char)key;
            status_line[used] = '\0';
            draw();
        }
        ui_flush(&ui);
    }
}
