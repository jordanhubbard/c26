/* MONITOR: a memory monitor and RV64 disassembler. It loads a C26FS file
 * (default the machine-assembled HI cartridge, or a name handed over IPC),
 * shows a hex dump, and disassembles the code — the natural companion to the
 * on-board assembler: RUN ASM to build a cartridge, RUN MONITOR to read it
 * back. A built-in self-test disassembles known instruction words at startup
 * so the decoder is machine-checkable. Talks only through the c26_api_t. */

#include "c26_api.h"

#define CART_HEADER_BYTES 32
#define VIEW_INSNS 14

static uint32_t width;
static uint32_t height;
static unsigned char file_buf[8192];
static int file_size;
static char filename[16] = "HI";
static int top;      /* first instruction shown (word index into code) */

static void format_int(int64_t value, char *out)
{
    char tmp[24];
    int n = 0;
    int negative = value < 0;
    uint64_t u = negative ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    if (u == 0) tmp[n++] = '0';
    while (u != 0) {
        tmp[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    }
    int k = 0;
    if (negative) out[k++] = '-';
    while (n > 0) out[k++] = tmp[--n];
    out[k] = '\0';
}

static void format_hex(uint64_t value, int digits, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < digits; i++) {
        out[digits - 1 - i] = hex[value & 0xf];
        value >>= 4;
    }
    out[digits] = '\0';
}

/* Tiny string builder for one disassembly line. */
static void put_s(char *b, int *n, const char *s)
{
    while (*s != '\0') b[(*n)++] = *s++;
}
static void put_d(char *b, int *n, int64_t v)
{
    char t[24];
    format_int(v, t);
    put_s(b, n, t);
}
static void put_reg(char *b, int *n, int r)
{
    b[(*n)++] = 'x';
    put_d(b, n, r);
}

/* Decode one RV64 instruction word into `out` (the subset apps/asm emits). */
static void disasm(uint32_t insn, char *out)
{
    int n = 0;
    uint32_t op = insn & 0x7f;
    int rd = (int)((insn >> 7) & 0x1f);
    int f3 = (int)((insn >> 12) & 7);
    int rs1 = (int)((insn >> 15) & 0x1f);
    int rs2 = (int)((insn >> 20) & 0x1f);
    uint32_t f7 = (insn >> 25) & 0x7f;
    int32_t immI = (int32_t)insn >> 20;
    int32_t immU = (int32_t)(insn & 0xfffff000u);
    int32_t immS = ((int32_t)(insn & 0xfe000000u) >> 20) |
                   (int32_t)((insn >> 7) & 0x1f);
    int32_t immB = (int32_t)(((insn >> 31) & 1) << 12) |
                   (int32_t)(((insn >> 7) & 1) << 11) |
                   (int32_t)(((insn >> 25) & 0x3f) << 5) |
                   (int32_t)(((insn >> 8) & 0xf) << 1);
    if (insn & 0x80000000u) immB |= (int32_t)0xffffe000u; /* sign extend */
    int32_t immJ = (int32_t)(((insn >> 31) & 1) << 20) |
                   (int32_t)(((insn >> 12) & 0xff) << 12) |
                   (int32_t)(((insn >> 20) & 1) << 11) |
                   (int32_t)(((insn >> 21) & 0x3ff) << 1);
    if (insn & 0x80000000u) immJ |= (int32_t)0xffe00000u;

    static const char *imm_ops[8] = {"addi", "slli", "slti", "sltiu",
                                     "xori", "srli", "ori", "andi"};
    static const char *reg_ops[8] = {"add", "sll", "slt", "sltu",
                                     "xor", "srl", "or", "and"};
    static const char *loads[8] = {"lb", "lh", "lw", "ld",
                                   "lbu", "lhu", "lwu", "?"};
    static const char *stores[8] = {"sb", "sh", "sw", "sd",
                                    "?", "?", "?", "?"};
    static const char *branches[8] = {"beq", "bne", "?", "?",
                                      "blt", "bge", "bltu", "bgeu"};

    switch (op) {
    case 0x37:
        put_s(out, &n, "lui ");
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_d(out, &n, (immU >> 12) & 0xfffff);
        break;
    case 0x17:
        put_s(out, &n, "auipc ");
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_d(out, &n, (immU >> 12) & 0xfffff);
        break;
    case 0x6f:
        put_s(out, &n, "jal ");
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_d(out, &n, immJ);
        break;
    case 0x67:
        if (rd == 0 && f3 == 0 && rs1 == 1 && immI == 0) {
            put_s(out, &n, "ret");
        } else {
            put_s(out, &n, "jalr ");
            put_reg(out, &n, rd);
            put_s(out, &n, ", ");
            put_d(out, &n, immI);
            out[n++] = '(';
            put_reg(out, &n, rs1);
            out[n++] = ')';
        }
        break;
    case 0x13:
        if (f3 == 0 && rd == 0 && rs1 == 0 && immI == 0) {
            put_s(out, &n, "nop");
            break;
        }
        put_s(out, &n, imm_ops[f3]);
        out[n++] = ' ';
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_reg(out, &n, rs1);
        put_s(out, &n, ", ");
        put_d(out, &n, (f3 == 1 || f3 == 5) ? (int)rs2 : immI);
        break;
    case 0x1b:
        put_s(out, &n, f3 == 0 ? "addiw" : (f3 == 1 ? "slliw" : "srliw"));
        out[n++] = ' ';
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_reg(out, &n, rs1);
        put_s(out, &n, ", ");
        put_d(out, &n, (f3 == 0) ? immI : (int)rs2);
        break;
    case 0x33: {
        static const char *m_ops[8] = {"mul", "mulh", "mulhsu", "mulhu",
                                       "div", "divu", "rem", "remu"};
        if (f7 == 0x01) put_s(out, &n, m_ops[f3]); /* M extension */
        else if (f3 == 0 && f7 == 0x20) put_s(out, &n, "sub");
        else if (f3 == 5 && f7 == 0x20) put_s(out, &n, "sra");
        else put_s(out, &n, reg_ops[f3]);
    }
        out[n++] = ' ';
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_reg(out, &n, rs1);
        put_s(out, &n, ", ");
        put_reg(out, &n, rs2);
        break;
    case 0x03:
        put_s(out, &n, loads[f3]);
        out[n++] = ' ';
        put_reg(out, &n, rd);
        put_s(out, &n, ", ");
        put_d(out, &n, immI);
        out[n++] = '(';
        put_reg(out, &n, rs1);
        out[n++] = ')';
        break;
    case 0x23:
        put_s(out, &n, stores[f3]);
        out[n++] = ' ';
        put_reg(out, &n, rs2);
        put_s(out, &n, ", ");
        put_d(out, &n, immS);
        out[n++] = '(';
        put_reg(out, &n, rs1);
        out[n++] = ')';
        break;
    case 0x63:
        put_s(out, &n, branches[f3]);
        out[n++] = ' ';
        put_reg(out, &n, rs1);
        put_s(out, &n, ", ");
        put_reg(out, &n, rs2);
        put_s(out, &n, ", ");
        put_d(out, &n, immB);
        break;
    case 0x73:
        put_s(out, &n, immI == 0 ? "ecall" : "ebreak");
        break;
    default: {
        char hx[9];
        format_hex(insn, 8, hx);
        put_s(out, &n, ".word 0x");
        put_s(out, &n, hx);
        break;
    }
    }
    out[n] = '\0';
}

/* Disassemble known words at startup so the decoder is machine-checkable. */
static void self_test(const c26_api_t *api)
{
    static const uint32_t words[] = {
        0x00A00513u, /* addi x10, x0, 10 */
        0x00B50533u, /* add  x10, x10, x11 */
        0x00008067u, /* ret */
    };
    api->puts("MONITOR SELFTEST\n");
    for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        char line[64];
        disasm(words[i], line);
        api->puts(line);
        api->putc('\n');
    }
}

static uint32_t word_at(int byte_off)
{
    if (byte_off + 3 >= file_size) return 0;
    return (uint32_t)file_buf[byte_off] |
           ((uint32_t)file_buf[byte_off + 1] << 8) |
           ((uint32_t)file_buf[byte_off + 2] << 16) |
           ((uint32_t)file_buf[byte_off + 3] << 24);
}

static void draw(const c26_api_t *api)
{
    api->fill_rect(0, 0, (int)width, (int)height, 0x0b1025);
    api->fill_rect(0, 0, (int)width, 24, 0x222957);
    char title[32];
    int t = 0;
    put_s(title, &t, "MONITOR ");
    put_s(title, &t, filename);
    title[t] = '\0';
    api->text(6, 4, title, 0xffffff, 0x222957, 2);
    api->text((int)width - 150, 8, "UP/DN Q QUIT", 0x9df6ff, 0x222957, 1);

    int code = CART_HEADER_BYTES; /* skip the cart header */
    int y = 30;
    for (int i = 0; i < VIEW_INSNS; i++) {
        int idx = top + i;
        int off = code + idx * 4;
        if (off + 3 >= file_size) break;
        uint32_t insn = word_at(off);
        char addr[9], hx[9], line[64];
        format_hex((uint64_t)(0x88000000u + (uint32_t)off), 8, addr);
        format_hex(insn, 8, hx);
        disasm(insn, line);
        api->text(6, y, addr, 0x6570bd, 0x0b1025, 1);
        api->text(90, y, hx, 0xbac4ff, 0x0b1025, 1);
        api->text(180, y, line, 0x68f0c0, 0x0b1025, 2);
        y += 20;
    }
}

int app_main(const c26_api_t *api)
{
    if (api->version < 2) return 1;
    api->window_size(&width, &height);
    api->puts("MONITOR CART ONLINE\n");
    self_test(api);

    /* A launcher may hand us a filename over IPC (like FILES -> EDIT). */
    uint64_t deadline = api->ticks() + 50;
    while (api->ticks() < deadline) {
        char message[16];
        int received = api->recv(0, message, sizeof(message) - 1);
        if (received > 0) {
            message[received] = '\0';
            for (int i = 0; i <= received && i < (int)sizeof(filename); i++) {
                filename[i] = message[i];
            }
            break;
        }
        api->idle();
    }

    size_t loaded = 0;
    if (api->fs_load(filename, file_buf, sizeof(file_buf), &loaded)) {
        file_size = (int)loaded;
    } else {
        file_size = 0;
    }
    api->puts("MONITOR ");
    api->puts(filename);
    api->putc(' ');
    api->put_int((int64_t)file_size);
    api->putc('\n');
    if (file_size > CART_HEADER_BYTES) {
        api->puts("MONITOR DISASM OK\n");
    }

    draw(api);
    api->present();

    int last_buttons = 0;
    int dirty = 0;
    uint64_t last_present = 0;
    int max_insns = (file_size - CART_HEADER_BYTES) / 4;
    for (;;) {
        if (api->stop_requested()) break;
        int ch = api->getchar();
        if (ch == 'Q' || ch == 0x1b) break;
        if ((ch == C26_KEY_DOWN || ch == 'j') && top < max_insns - 1) {
            top++;
            dirty = 1;
        }
        if ((ch == C26_KEY_UP || ch == 'k') && top > 0) {
            top--;
            dirty = 1;
        }
        int x, y, buttons;
        api->mouse(&x, &y, &buttons);
        (void)x;
        (void)y;
        last_buttons = buttons & 1;
        (void)last_buttons;

        uint64_t now = api->ticks();
        if (dirty && now - last_present >= 2) {
            last_present = now;
            dirty = 0;
            draw(api);
            api->present();
        }
        api->idle();
    }
    api->puts("MONITOR CART EXIT\n");
    return 0;
}
