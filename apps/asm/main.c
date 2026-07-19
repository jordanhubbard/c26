/* ASM: the self-hosting moment. A two-pass RV64I assembler that runs ON
 * the machine: reads an assembly source file from C26FS, emits a runnable
 * cartridge (header + code), and saves it back to disk — where RUN will
 * happily execute it as a protected process. Type "SOURCE DEST" and press
 * Enter; Q quits.
 *
 * Mnemonics: LUI AUIPC ADDI ADDIW ANDI ORI XORI ADD SUB AND OR XOR
 * LD LW LB LBU SD SW SB BEQ BNE BLT BGE JAL JALR and pseudos
 * MV LI LA J CALL RET NOP, plus .ASCIZ .BYTE .WORD .QUAD .ALIGN and
 * LABEL: definitions. Comments start with ';'. Programs are entered with
 * the api pointer in A0 and return with RET. */

#include "ui.h"

#define SOURCE_MAX 8000
#define OUTPUT_MAX 8000
#define SYMBOL_MAX 64
#define CART_BASE 0x88000000UL

static c26_ui_t ui;
static const c26_api_t *g_api;
static char source[SOURCE_MAX + 1];
static uint8_t output[OUTPUT_MAX];
static char status_line[64];
static char error_line[64];

typedef struct {
    char name[16];
    uint32_t offset;
} symbol_t;

static symbol_t symbols[SYMBOL_MAX];
static int symbol_count;
static uint32_t cursor;   /* bytes emitted (header included) */
static int pass;
static int error_at;      /* source line number, 0 = ok */
static const char *error_text;

/* ------------------------------------------------------------------ */
/* Small string helpers (freestanding)                                 */

static int str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] != '\0' && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* Emission                                                            */

static void fail(int line, const char *why)
{
    if (error_at == 0) {
        error_at = line;
        error_text = why;
    }
}

static void emit8(uint8_t byte)
{
    if (cursor < OUTPUT_MAX) {
        if (pass == 2) output[cursor] = byte;
    }
    cursor++;
}

static void emit32(uint32_t word)
{
    emit8((uint8_t)word);
    emit8((uint8_t)(word >> 8));
    emit8((uint8_t)(word >> 16));
    emit8((uint8_t)(word >> 24));
}

/* ------------------------------------------------------------------ */
/* Symbols and operand parsing                                         */

static int find_symbol(const char *name, uint32_t *offset)
{
    for (int i = 0; i < symbol_count; i++) {
        if (str_eq(symbols[i].name, name)) {
            *offset = symbols[i].offset;
            return 1;
        }
    }
    return 0;
}

static void define_symbol(const char *name, int line)
{
    if (pass != 1) return;
    uint32_t existing;
    if (find_symbol(name, &existing)) {
        fail(line, "DUPLICATE LABEL");
        return;
    }
    if (symbol_count == SYMBOL_MAX) {
        fail(line, "TOO MANY LABELS");
        return;
    }
    str_copy(symbols[symbol_count].name, name, 16);
    symbols[symbol_count].offset = cursor;
    symbol_count++;
}

static const char *reg_names[32][2] = {
    {"X0", "ZERO"}, {"X1", "RA"}, {"X2", "SP"}, {"X3", "GP"},
    {"X4", "TP"}, {"X5", "T0"}, {"X6", "T1"}, {"X7", "T2"},
    {"X8", "S0"}, {"X9", "S1"}, {"X10", "A0"}, {"X11", "A1"},
    {"X12", "A2"}, {"X13", "A3"}, {"X14", "A4"}, {"X15", "A5"},
    {"X16", "A6"}, {"X17", "A7"}, {"X18", "S2"}, {"X19", "S3"},
    {"X20", "S4"}, {"X21", "S5"}, {"X22", "S6"}, {"X23", "S7"},
    {"X24", "S8"}, {"X25", "S9"}, {"X26", "S10"}, {"X27", "S11"},
    {"X28", "T3"}, {"X29", "T4"}, {"X30", "T5"}, {"X31", "T6"},
};

static int parse_register(const char *token)
{
    for (int i = 0; i < 32; i++) {
        if (str_eq(token, reg_names[i][0]) || str_eq(token, reg_names[i][1]))
            return i;
    }
    if (str_eq(token, "FP")) return 8;
    return -1;
}

static int64_t parse_number(const char *token, int *ok)
{
    int64_t sign = 1;
    *ok = 0;
    if (*token == '-') {
        sign = -1;
        token++;
    }
    int64_t value = 0;
    if (token[0] == '0' && token[1] == 'X') {
        token += 2;
        while (*token != '\0') {
            int digit;
            if (*token >= '0' && *token <= '9') digit = *token - '0';
            else if (*token >= 'A' && *token <= 'F') digit = *token - 'A' + 10;
            else return 0;
            value = value * 16 + digit;
            token++;
            *ok = 1;
        }
    } else {
        while (*token >= '0' && *token <= '9') {
            value = value * 10 + (*token - '0');
            token++;
            *ok = 1;
        }
        if (*token != '\0') *ok = 0;
    }
    return sign * value;
}

/* Value of an operand that may be a number or a label (label = absolute
 * cart address). */
/* Evaluate one term (a number or a symbol) into *value; returns 0 on an
   undefined symbol in pass 2. */
static int term_value(const char *term, int line, int64_t *value)
{
    int ok;
    *value = parse_number(term, &ok);
    if (ok) return 1;
    uint32_t offset;
    if (find_symbol(term, &offset)) {
        *value = (int64_t)offset;
        return 1;
    }
    if (pass == 1) { /* forward reference: any value works for sizing */
        *value = 0;
        return 1;
    }
    fail(line, "UNDEFINED SYMBOL");
    return 0;
}

/* Evaluate an expression operand: terms (numbers or symbols) joined by
   + - * and applied left to right, e.g. MSG+4, COUNT*2, END-START. */
static int64_t eval_expr(const char *token, int line)
{
    int64_t acc = 0;
    char op = '+';
    int i = 0;
    while (token[i] != '\0') {
        char term[24];
        int j = 0;
        while (token[i] != '\0' && token[i] != '+' && token[i] != '-' &&
               token[i] != '*' && j < 23) {
            term[j++] = token[i++];
        }
        term[j] = '\0';
        int64_t v = 0;
        term_value(term, line, &v);
        if (op == '+') acc += v;
        else if (op == '-') acc -= v;
        else acc *= v;
        if (token[i] == '\0') break;
        op = token[i++];
    }
    return acc;
}

static int64_t operand_value(const char *token, int line, int *is_label,
                             uint32_t *label_offset)
{
    int ok;
    int64_t value = parse_number(token, &ok);
    if (ok) {
        if (is_label != 0) *is_label = 0;
        return value;
    }
    uint32_t offset;
    if (find_symbol(token, &offset)) {
        if (is_label != 0) *is_label = 1;
        if (label_offset != 0) *label_offset = offset;
        return (int64_t)offset;
    }
    /* An expression operand: a symbol or number followed by + - * and more. */
    for (int i = 1; token[i] != '\0'; i++) {
        if (token[i] == '+' || token[i] == '-' || token[i] == '*') {
            if (is_label != 0) *is_label = 0;
            return eval_expr(token, line);
        }
    }
    if (pass == 1) {
        /* Forward reference: size is what matters in pass 1. */
        if (is_label != 0) *is_label = 1;
        if (label_offset != 0) *label_offset = cursor;
        return 0;
    }
    fail(line, "UNDEFINED SYMBOL");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Instruction encoders                                                */

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

/* ------------------------------------------------------------------ */
/* Line assembly                                                       */

#define TOKEN_MAX 6

static int need_register(const char *token, int line)
{
    int reg = parse_register(token);
    if (reg < 0) fail(line, "BAD REGISTER");
    return reg < 0 ? 0 : reg;
}

/* Splits "OFF(REG)" into offset expression and register. */
static int memory_operand(const char *token, int line, int64_t *offset)
{
    char imm_part[24];
    char reg_part[8];
    int i = 0;
    while (token[i] != '(' && token[i] != '\0' && i < 23) {
        imm_part[i] = token[i];
        i++;
    }
    imm_part[i] = '\0';
    if (token[i] != '(') {
        fail(line, "EXPECTED OFF(REG)");
        return 0;
    }
    int j = 0;
    i++;
    while (token[i] != ')' && token[i] != '\0' && j < 7) {
        reg_part[j++] = token[i++];
    }
    reg_part[j] = '\0';
    int ok;
    *offset = imm_part[0] == '\0' ? 0 : parse_number(imm_part, &ok);
    if (imm_part[0] != '\0' && !ok) fail(line, "BAD OFFSET");
    return need_register(reg_part, line);
}

static void li_pseudo(int rd, int64_t imm)
{
    if (imm >= -2048 && imm < 2048) {
        emit32(enc_i(0x13, 0, rd, 0, imm));         /* addi rd, x0, imm */
        return;
    }
    int64_t hi = (imm + 0x800) >> 12;
    int64_t lo = imm - (hi << 12);
    emit32(enc_u(0x37, rd, hi));                    /* lui */
    emit32(enc_i(0x1b, 0, rd, rd, lo));             /* addiw */
}

static void la_pseudo(int rd, int64_t target_offset)
{
    int64_t delta = (int64_t)target_offset - (int64_t)cursor;
    int64_t hi = (delta + 0x800) >> 12;
    int64_t lo = delta - (hi << 12);
    emit32(enc_u(0x17, rd, hi));                    /* auipc */
    emit32(enc_i(0x13, 0, rd, rd, lo));             /* addi */
}

static void assemble_line(char tokens[TOKEN_MAX][24], int token_count,
                          const char *string_literal, int line)
{
    const char *m = tokens[0];
    char (*t)[24] = tokens;

    if (str_eq(m, ".ASCIZ")) {
        if (string_literal == 0) {
            fail(line, "EXPECTED STRING");
            return;
        }
        for (int i = 0; string_literal[i] != '\0'; i++) {
            if (string_literal[i] == '\\' && string_literal[i + 1] == 'N') {
                emit8('\n');
                i++;
            } else {
                emit8((uint8_t)string_literal[i]);
            }
        }
        emit8(0);
        return;
    }
    if (str_eq(m, ".ALIGN")) {
        while ((cursor & 3) != 0) emit8(0);
        return;
    }
    if (str_eq(m, ".BYTE") || str_eq(m, ".WORD") || str_eq(m, ".QUAD")) {
        int width = str_eq(m, ".BYTE") ? 1 : (str_eq(m, ".WORD") ? 4 : 8);
        for (int i = 1; i < token_count; i++) {
            int64_t value = operand_value(t[i], line, 0, 0);
            for (int b = 0; b < width; b++) emit8((uint8_t)(value >> (8 * b)));
        }
        return;
    }

    if (str_eq(m, "NOP")) { emit32(enc_i(0x13, 0, 0, 0, 0)); return; }
    if (str_eq(m, "RET")) { emit32(enc_i(0x67, 0, 0, 1, 0)); return; }
    if (str_eq(m, "MV")) {
        int rd = need_register(t[1], line);
        int rs = need_register(t[2], line);
        emit32(enc_i(0x13, 0, rd, rs, 0));
        return;
    }
    if (str_eq(m, "LI")) {
        int rd = need_register(t[1], line);
        int ok;
        int64_t imm = parse_number(t[2], &ok);
        if (!ok) fail(line, "BAD IMMEDIATE");
        /* Pass-1 size must match pass 2: decide by value both times. */
        li_pseudo(rd, imm);
        return;
    }
    if (str_eq(m, "LA")) {
        int rd = need_register(t[1], line);
        uint32_t target = 0;
        int is_label = 0;
        operand_value(t[2], line, &is_label, &target);
        if (!is_label) fail(line, "EXPECTED LABEL");
        la_pseudo(rd, target);
        return;
    }
    if (str_eq(m, "J") || str_eq(m, "JAL") || str_eq(m, "CALL")) {
        int rd = str_eq(m, "J") ? 0 : 1;
        int label_index = 1;
        if (str_eq(m, "JAL") && token_count > 2) {
            rd = need_register(t[1], line);
            label_index = 2;
        }
        uint32_t target = 0;
        int is_label = 0;
        operand_value(t[label_index], line, &is_label, &target);
        if (!is_label) fail(line, "EXPECTED LABEL");
        emit32(enc_j(rd, (int64_t)target - (int64_t)cursor));
        return;
    }
    if (str_eq(m, "JALR")) {
        /* JALR rs | JALR rd, rs | JALR rd, off(rs) */
        if (token_count == 2) {
            int rs = need_register(t[1], line);
            emit32(enc_i(0x67, 0, 1, rs, 0));
        } else {
            int rd = need_register(t[1], line);
            int rs = need_register(t[2], line);
            emit32(enc_i(0x67, 0, rd, rs, 0));
        }
        return;
    }
    if (str_eq(m, "LUI") || str_eq(m, "AUIPC")) {
        int rd = need_register(t[1], line);
        int64_t imm = operand_value(t[2], line, 0, 0);
        emit32(enc_u(str_eq(m, "LUI") ? 0x37 : 0x17, rd, imm));
        return;
    }

    static const struct { const char *name; int f3; } itype[] = {
        {"ADDI", 0}, {"XORI", 4}, {"ORI", 6}, {"ANDI", 7}, {"SLTI", 2},
    };
    for (unsigned int i = 0; i < sizeof(itype) / sizeof(itype[0]); i++) {
        if (str_eq(m, itype[i].name)) {
            int rd = need_register(t[1], line);
            int rs = need_register(t[2], line);
            int64_t imm = operand_value(t[3], line, 0, 0);
            emit32(enc_i(0x13, itype[i].f3, rd, rs, imm));
            return;
        }
    }
    if (str_eq(m, "ADDIW")) {
        int rd = need_register(t[1], line);
        int rs = need_register(t[2], line);
        int64_t imm = operand_value(t[3], line, 0, 0);
        emit32(enc_i(0x1b, 0, rd, rs, imm));
        return;
    }
    if (str_eq(m, "SLLI") || str_eq(m, "SRLI")) {
        int rd = need_register(t[1], line);
        int rs = need_register(t[2], line);
        int64_t sh = operand_value(t[3], line, 0, 0);
        emit32(enc_i(0x13, str_eq(m, "SLLI") ? 1 : 5, rd, rs, sh & 0x3f));
        return;
    }
    static const struct { const char *name; int f3; int f7; } rtype[] = {
        {"ADD", 0, 0}, {"SUB", 0, 0x20}, {"AND", 7, 0}, {"OR", 6, 0},
        {"XOR", 4, 0}, {"SLL", 1, 0}, {"SRL", 5, 0},
    };
    for (unsigned int i = 0; i < sizeof(rtype) / sizeof(rtype[0]); i++) {
        if (str_eq(m, rtype[i].name)) {
            int rd = need_register(t[1], line);
            int rs1 = need_register(t[2], line);
            int rs2 = need_register(t[3], line);
            emit32(enc_r(rtype[i].f7, rs2, rs1, rtype[i].f3, rd, 0x33));
            return;
        }
    }
    static const struct { const char *name; int f3; } loads[] = {
        {"LB", 0}, {"LW", 2}, {"LD", 3}, {"LBU", 4},
    };
    for (unsigned int i = 0; i < sizeof(loads) / sizeof(loads[0]); i++) {
        if (str_eq(m, loads[i].name)) {
            int rd = need_register(t[1], line);
            int64_t offset;
            int rs = memory_operand(t[2], line, &offset);
            emit32(enc_i(0x03, loads[i].f3, rd, rs, offset));
            return;
        }
    }
    static const struct { const char *name; int f3; } stores[] = {
        {"SB", 0}, {"SW", 2}, {"SD", 3},
    };
    for (unsigned int i = 0; i < sizeof(stores) / sizeof(stores[0]); i++) {
        if (str_eq(m, stores[i].name)) {
            int rs2 = need_register(t[1], line);
            int64_t offset;
            int rs1 = memory_operand(t[2], line, &offset);
            emit32(enc_s(stores[i].f3, rs2, rs1, offset));
            return;
        }
    }
    static const struct { const char *name; int f3; } branches[] = {
        {"BEQ", 0}, {"BNE", 1}, {"BLT", 4}, {"BGE", 5},
    };
    for (unsigned int i = 0; i < sizeof(branches) / sizeof(branches[0]); i++) {
        if (str_eq(m, branches[i].name)) {
            int rs1 = need_register(t[1], line);
            int rs2 = need_register(t[2], line);
            uint32_t target = 0;
            int is_label = 0;
            operand_value(t[3], line, &is_label, &target);
            if (!is_label) fail(line, "EXPECTED LABEL");
            emit32(enc_b(branches[i].f3, rs1, rs2,
                         (int64_t)target - (int64_t)cursor));
            return;
        }
    }
    fail(line, "UNKNOWN MNEMONIC");
}

/* ------------------------------------------------------------------ */
/* Pass driver                                                         */

static void run_pass(void)
{
    cursor = 32; /* the cart header comes first */
    int line = 0;
    const char *p = source;
    while (*p != '\0' && error_at == 0) {
        line++;
        char tokens[TOKEN_MAX][24];
        char literal[128];
        const char *string_literal = 0;
        int token_count = 0;
        /* Tokenize one line. */
        while (*p == ' ' || *p == '\t') p++;
        while (*p != '\0' && *p != '\n' && *p != ';') {
            if (*p == '"') {
                p++;
                int i = 0;
                while (*p != '"' && *p != '\0' && *p != '\n' && i < 127) {
                    literal[i++] = *p++;
                }
                literal[i] = '\0';
                if (*p == '"') p++;
                string_literal = literal;
            } else {
                char token[24];
                int i = 0;
                while (*p != '\0' && *p != '\n' && *p != ' ' && *p != '\t' &&
                       *p != ',' && *p != ';' && *p != '"' && i < 23) {
                    token[i++] = *p++;
                }
                token[i] = '\0';
                if (i > 0 && token[i - 1] == ':') {
                    token[i - 1] = '\0';
                    define_symbol(token, line);
                } else if (i > 0 && token_count < TOKEN_MAX) {
                    str_copy(tokens[token_count++], token, 24);
                }
            }
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
        if (token_count > 0) {
            for (int i = token_count; i < TOKEN_MAX; i++) tokens[i][0] = '\0';
            assemble_line(tokens, token_count, string_literal, line);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Preprocessor: .MACRO/.ENDM with \1..\9 args, macro invocation, and
 * .INCLUDE, all expanded into `source` before the two assembly passes. */

#define MACRO_MAX 16
#define MACRO_BODY_MAX 2048
#define INCLUDE_MAX 4000
static struct {
    char name[24];
    int start;
    int len;
} macros[MACRO_MAX];
static int macro_count;
static char macro_bodies[MACRO_BODY_MAX];
static int macro_body_used;
static char expanded[SOURCE_MAX + 1];
static char include_buf[INCLUDE_MAX];

static int find_macro(const char *name)
{
    for (int i = 0; i < macro_count; i++) {
        if (str_eq(macros[i].name, name)) return i;
    }
    return -1;
}

/* Split [s,e) into up to 9 whitespace/comma-separated arg tokens. */
static int split_args(const char *s, const char *e, char args[9][24])
{
    int n = 0;
    while (s < e && n < 9) {
        while (s < e && (*s == ' ' || *s == '\t' || *s == ',')) s++;
        if (s >= e) break;
        int j = 0;
        while (s < e && *s != ' ' && *s != '\t' && *s != ',' && j < 23) {
            args[n][j++] = *s++;
        }
        args[n][j] = '\0';
        n++;
    }
    return n;
}

static void emit_range(int *out, const char *s, const char *e)
{
    while (s < e && *out < SOURCE_MAX) expanded[(*out)++] = *s++;
    if (*out < SOURCE_MAX) expanded[(*out)++] = '\n';
}

/* Emit macro body with \1..\9 replaced by the invocation's args. */
static void expand_macro(int mi, const char *argstart, const char *argend,
                         int *out)
{
    char args[9][24];
    for (int i = 0; i < 9; i++) args[i][0] = '\0';
    split_args(argstart, argend, args);
    const char *b = &macro_bodies[macros[mi].start];
    int len = macros[mi].len;
    for (int i = 0; i < len && *out < SOURCE_MAX; i++) {
        if (b[i] == '\\' && i + 1 < len && b[i + 1] >= '1' && b[i + 1] <= '9') {
            const char *a = args[b[i + 1] - '1'];
            while (*a != '\0' && *out < SOURCE_MAX) expanded[(*out)++] = *a++;
            i++;
        } else {
            expanded[(*out)++] = b[i];
        }
    }
}

static void preprocess(void)
{
    macro_count = 0;
    macro_body_used = 0;
    int out = 0;
    int in_macro = -1;
    const char *p = source;
    while (*p != '\0') {
        const char *ls = p;
        const char *le = p;
        while (*le != '\0' && *le != '\n') le++;
        const char *q = ls;
        while (q < le && (*q == ' ' || *q == '\t')) q++;
        char first[24];
        int fi = 0;
        while (q < le && *q != ' ' && *q != '\t' && fi < 23) first[fi++] = *q++;
        first[fi] = '\0';
        const char *rest = q;
        while (rest < le && (*rest == ' ' || *rest == '\t')) rest++;

        if (in_macro >= 0) {
            if (str_eq(first, ".ENDM")) {
                macros[in_macro].len = macro_body_used - macros[in_macro].start;
                in_macro = -1;
            } else {
                const char *s = ls;
                while (s < le && macro_body_used < MACRO_BODY_MAX) {
                    macro_bodies[macro_body_used++] = *s++;
                }
                if (macro_body_used < MACRO_BODY_MAX) {
                    macro_bodies[macro_body_used++] = '\n';
                }
            }
        } else if (str_eq(first, ".MACRO")) {
            char args[9][24];
            int n = split_args(rest, le, args);
            if (n >= 1 && macro_count < MACRO_MAX) {
                in_macro = macro_count++;
                str_copy(macros[in_macro].name, args[0], 24);
                macros[in_macro].start = macro_body_used;
                macros[in_macro].len = 0;
            }
        } else if (str_eq(first, ".INCLUDE")) {
            char args[9][24];
            split_args(rest, le, args);
            char fn[16];
            int k = 0;
            for (const char *a = args[0]; *a != '\0' && k < 15; a++) {
                if (*a != '"') fn[k++] = *a;
            }
            fn[k] = '\0';
            size_t isz = 0;
            if (g_api->fs_load(fn, include_buf, sizeof(include_buf) - 1, &isz)) {
                for (size_t i = 0; i < isz; i++) {
                    char c = include_buf[i];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    if (out < SOURCE_MAX) expanded[out++] = c;
                }
                if (out < SOURCE_MAX) expanded[out++] = '\n';
            }
        } else {
            int mi = find_macro(first);
            if (mi >= 0) {
                expand_macro(mi, rest, le, &out);
            } else {
                emit_range(&out, ls, le);
            }
        }
        p = (*le == '\n') ? le + 1 : le;
    }
    expanded[out] = '\0';
    for (int i = 0; i <= out; i++) source[i] = expanded[i];
}

static int assemble(const char *src_name, const char *dst_name)
{
    size_t size = 0;
    if (!g_api->fs_load(src_name, source, SOURCE_MAX, &size)) {
        str_copy(error_line, "CANNOT LOAD SOURCE", 64);
        return 0;
    }
    source[size] = '\0';
    for (size_t i = 0; i < size; i++) {
        if (source[i] >= 'a' && source[i] <= 'z') source[i] -= 32;
    }
    preprocess();
    symbol_count = 0;
    error_at = 0;
    error_text = "";
    pass = 1;
    run_pass();
    pass = 2;
    run_pass();
    if (error_at != 0) {
        char *e = error_line;
        const char *w = error_text;
        int i = 0;
        while (w[i] != '\0' && i < 40) { e[i] = w[i]; i++; }
        e[i++] = ' '; e[i++] = 'L';
        if (error_at >= 10) e[i++] = (char)('0' + (error_at / 10) % 10);
        e[i++] = (char)('0' + error_at % 10);
        e[i] = '\0';
        return 0;
    }
    if (cursor > OUTPUT_MAX) {
        str_copy(error_line, "PROGRAM TOO LARGE", 64);
        return 0;
    }
    /* Cartridge header: magic, ABI version, load size, bss, entry. */
    uint32_t header[8] = {0x54524143U, 2U, cursor, 0U, 32U, 0U, 0U, 0U};
    for (int i = 0; i < 8; i++) {
        output[i * 4] = (uint8_t)header[i];
        output[i * 4 + 1] = (uint8_t)(header[i] >> 8);
        output[i * 4 + 2] = (uint8_t)(header[i] >> 16);
        output[i * 4 + 3] = (uint8_t)(header[i] >> 24);
    }
    if (!g_api->fs_save(dst_name, output, cursor)) {
        str_copy(error_line, "CANNOT SAVE OUTPUT", 64);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* UI                                                                  */

static void draw(void)
{
    ui_clear(&ui);
    ui_titlebar(&ui, "ASM", "SRC DST + ENTER  Q QUIT");
    ui_text(&ui, 12, 44, "TWO-PASS RV64 ASSEMBLER", UI_TEXT);
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
    api->puts("ASM CART ONLINE\n");
    int used = 0;
    draw();

    for (;;) {
        int key = ui_poll(&ui);
        if (key == -2) return 0;
        if (key == 'Q' && used == 0) {
            api->puts("ASM CART EXIT\n");
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
            if (src[0] != '\0' && dst[0] != '\0' && assemble(src, dst)) {
                api->puts("ASSEMBLED ");
                api->puts(src);
                api->puts(" -> ");
                api->puts(dst);
                api->puts(" (");
                api->put_int((int64_t)cursor);
                api->puts(" BYTES)\n");
                str_copy(error_line, "OK - RUN IT", 64);
            } else if (error_line[0] != '\0') {
                api->puts("ASM ERROR: ");
                api->puts(error_line);
                api->putc('\n');
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
