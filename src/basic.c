#include "c26.h"

typedef struct {
    const char *name;
    uint64_t value;
} basic_var_t;

static basic_var_t vars[8];

static basic_var_t *var_for(const char *name)
{
    for (size_t i = 0; i < 8; i++) {
        if (vars[i].name == name || vars[i].name == 0) {
            vars[i].name = name;
            return &vars[i];
        }
    }
    return &vars[0];
}

static void run_let(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 3);
    const char *name = cursor;
    while (*cursor != '\0' && *cursor != '=') {
        cursor++;
    }
    if (*cursor == '=') {
        cursor++;
    }
    var_for(name)->value = c26_parse_uint(&cursor);
}

static void run_print(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 5);
    if (*cursor == '"') {
        cursor++;
        while (*cursor != '\0' && *cursor != '"') {
            c26_uart_putc(*cursor++);
        }
        c26_uart_putc('\n');
        return;
    }
    uint64_t left = c26_parse_uint(&cursor);
    cursor = c26_skip_spaces(cursor);
    if (*cursor == '+') {
        cursor++;
        left += c26_parse_uint(&cursor);
    }
    c26_put_uint(left);
    c26_uart_putc('\n');
}

static void run_poke(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 4);
    uint64_t address = c26_parse_uint(&cursor);
    if (*cursor == ',') {
        cursor++;
    }
    uint64_t value = c26_parse_uint(&cursor);
    c26_puts("POKE ");
    c26_put_hex(address);
    c26_puts(", ");
    c26_put_uint(value);
    c26_puts(" (emulated)\n");
}

static void run_line(const char *line)
{
    while (*line >= '0' && *line <= '9') {
        line++;
    }
    line = c26_skip_spaces(line);
    if (c26_starts_with(line, "PRINT")) {
        run_print(line);
    } else if (c26_starts_with(line, "LET")) {
        run_let(line);
    } else if (c26_starts_with(line, "POKE")) {
        run_poke(line);
    } else if (c26_starts_with(line, "PEEK")) {
        c26_puts("PEEK returned emulated device byte 26\n");
    }
}

void c26_basic_demo(void)
{
    static const char *program[] = {
        "10 PRINT \"BASIC READY\"",
        "20 PRINT \"HELLO FROM C26\"",
        "30 PRINT 20+6",
        "40 POKE 53280,26",
        "50 PEEK 53280",
        0,
    };
    c26_puts("C26 BASIC V0.1\n");
    for (size_t i = 0; program[i] != 0; i++) {
        c26_puts("] ");
        c26_puts(program[i]);
        c26_uart_putc('\n');
        run_line(program[i]);
    }
}
