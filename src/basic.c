#include "c26.h"
#include "c26_devices.h"

static uint64_t vars[26];
static char input_line[96];
static size_t input_length;

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
    c26_device_write8((uint16_t)(address & 0xff), (uint8_t)value);
    c26_puts("POKE compatibility alias used DEVICE WRITE\n");
}

static void run_peek(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 4);
    uint64_t address = c26_parse_uint(&cursor);
    uint8_t value = 0;
    c26_device_read8((uint16_t)(address & 0xff), &value);
    c26_puts("PEEK compatibility alias returned ");
    c26_put_uint(value);
    c26_uart_putc('\n');
}

static void run_device(const char *line)
{
    const char *cursor = c26_skip_spaces(line + 6);
    if (c26_starts_with(cursor, "WRITE")) {
        cursor = c26_skip_spaces(cursor + 5);
        uint64_t reg = c26_parse_uint(&cursor);
        uint64_t value = c26_parse_uint(&cursor);
        if (c26_device_write8((uint16_t)reg, (uint8_t)value)) {
            c26_puts("DEVICE WRITE OK\n");
        } else {
            c26_puts("Error: invalid device register\n");
        }
    } else if (c26_starts_with(cursor, "READ")) {
        cursor = c26_skip_spaces(cursor + 4);
        uint64_t reg = c26_parse_uint(&cursor);
        uint8_t value = 0;
        if (c26_device_read8((uint16_t)reg, &value)) {
            c26_puts("DEVICE READ returned ");
            c26_put_uint(value);
            c26_uart_putc('\n');
        } else {
            c26_puts("Error: invalid device register\n");
        }
    } else {
        c26_puts("Usage: DEVICE READ reg | DEVICE WRITE reg value\n");
    }
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
        run_peek(line);
    } else if (c26_starts_with(line, "DEVICE")) {
        run_device(line);
    } else if (c26_starts_with(line, "HELP")) {
        c26_puts("PRINT LET DEVICE READ DEVICE WRITE PEEK POKE HELP\n");
    } else if (*line != '\0') {
        c26_puts("Error: unknown command. Type HELP.\n");
    }
}

static void prompt(void)
{
    c26_puts("] ");
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
        "70 DEVICE WRITE 128 26",
        "80 DEVICE READ 128",
        0,
    };

    c26_puts("C26 BASIC V1.0\n");

    for (size_t i = 0; program[i] != 0; i++) {
        c26_puts("] ");
        c26_puts(program[i]);
        c26_uart_putc('\n');
        run_line(program[i]);
    }
    c26_puts("BASIC INTERACTIVE READY - TYPE HELP\n");
    input_length = 0;
    prompt();
}

void c26_basic_feed_char(char ch)
{
    if (ch == '\r' || ch == '\n') {
        c26_uart_putc('\n');
        input_line[input_length] = '\0';
        run_line(input_line);
        input_length = 0;
        prompt();
        return;
    }
    if (ch == '\b' || ch == 0x7f) {
        if (input_length != 0) {
            input_length--;
            c26_puts("\b \b");
        }
        return;
    }
    if (ch < 32 || ch > 126 || input_length + 1 >= sizeof(input_line)) {
        return;
    }
    if (ch >= 'a' && ch <= 'z') {
        ch -= ('a' - 'A');
    }
    input_line[input_length++] = ch;
    c26_uart_putc(ch);
}
