#include "c26.h"

static uint64_t vars[26];

static int var_index(char name)
{
    if (name >= 'a' && name <= 'z') {
        name -= ('a' - 'A');
    }
    if (name < 'A' || name > 'Z') {
        return -1;
    }
    return (int)(name - 'A');
}

static uint64_t parse_value(const char **cursor)
{
    *cursor = c26_skip_spaces(*cursor);

    int index = var_index(**cursor);
    if (index >= 0) {
        (*cursor)++;
        return vars[index];
    }

    return c26_parse_uint(cursor);
}

static void run_let(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 3);
    int index = var_index(*cursor);
    if (index < 0) {
        c26_puts("Error: LET variable must be a single letter A-Z\n");
        return;
    }
    cursor++;
    cursor = c26_skip_spaces(cursor);
    if (*cursor != '=') {
        c26_puts("Error: LET missing '='\n");
        return;
    }
    cursor++;
    vars[index] = parse_value(&cursor);
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

    uint64_t left = parse_value(&cursor);
    cursor = c26_skip_spaces(cursor);
    if (*cursor == '+') {
        cursor++;
        left += parse_value(&cursor);
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
        "10 LET A=26",
        "20 PRINT A",
        "30 PRINT A+16",
        "40 PRINT \"BASIC READY\"",
        "50 PRINT \"HELLO FROM C26\"",
        "60 PRINT 20+6",
        "70 POKE 53280,26",
        "80 PEEK 53280",
        0,
    };

    c26_puts("C26 BASIC V0.2\n");

    for (size_t i = 0; program[i] != 0; i++) {
        c26_puts("] ");
        c26_puts(program[i]);
        c26_uart_putc('\n');
        run_line(program[i]);
    }
}
