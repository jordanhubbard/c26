#include "c26.h"
#include "c26_devices.h"
#include "c26_fs.h"

#define BASIC_LINE_COUNT 64U
#define BASIC_LINE_LENGTH 64U

typedef struct {
    uint16_t number;
    char text[BASIC_LINE_LENGTH];
} basic_line_t;

static uint64_t vars[26];
static char input_line[96];
static size_t input_length;
static basic_line_t program[BASIC_LINE_COUNT];
static size_t program_count;
static char file_buffer[C26_FS_FILE_MAX + 1];

static int var_index(char name)
{
    if (name >= 'a' && name <= 'z') name -= ('a' - 'A');
    if (name < 'A' || name > 'Z') return -1;
    return (int)(name - 'A');
}

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') length++;
    return length;
}

static int keyword(const char *line, const char *word)
{
    while (*word != '\0' && *line == *word) {
        line++;
        word++;
    }
    return *word == '\0' && (*line == '\0' || *line == ' ' || *line == '\t');
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
        while (*cursor != '\0' && *cursor != '"') c26_uart_putc(*cursor++);
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
    if (*cursor == ',') cursor++;
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
    if (keyword(cursor, "WRITE")) {
        cursor = c26_skip_spaces(cursor + 5);
        uint64_t reg = c26_parse_uint(&cursor);
        uint64_t value = c26_parse_uint(&cursor);
        if (c26_device_write8((uint16_t)reg, (uint8_t)value))
            c26_puts("DEVICE WRITE OK\n");
        else
            c26_puts("Error: invalid device register\n");
    } else if (keyword(cursor, "READ")) {
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

static int store_line(uint16_t number, const char *text)
{
    size_t position = 0;
    while (position < program_count && program[position].number < number) {
        position++;
    }
    if (position < program_count && program[position].number == number) {
        if (*text == '\0') {
            for (size_t i = position + 1; i < program_count; i++)
                program[i - 1] = program[i];
            program_count--;
            return 2;
        }
    } else {
        if (*text == '\0') return 0;
        if (program_count == BASIC_LINE_COUNT) return 0;
        for (size_t i = program_count; i > position; i--)
            program[i] = program[i - 1];
        program_count++;
    }
    program[position].number = number;
    size_t length = text_length(text);
    if (length >= BASIC_LINE_LENGTH) length = BASIC_LINE_LENGTH - 1;
    memcpy(program[position].text, text, length);
    program[position].text[length] = '\0';
    return 1;
}

static void list_program(void)
{
    for (size_t i = 0; i < program_count; i++) {
        c26_put_uint(program[i].number);
        c26_uart_putc(' ');
        c26_puts(program[i].text);
        c26_uart_putc('\n');
    }
}

static void run_statement(const char *line)
{
    line = c26_skip_spaces(line);
    if (keyword(line, "PRINT")) run_print(line);
    else if (keyword(line, "LET")) run_let(line);
    else if (keyword(line, "POKE")) run_poke(line);
    else if (keyword(line, "PEEK")) run_peek(line);
    else if (keyword(line, "DEVICE")) run_device(line);
    else if (*line != '\0') c26_puts("Error: unknown statement\n");
}

static void run_program(void)
{
    c26_puts("RUN\n");
    for (size_t i = 0; i < program_count; i++) run_statement(program[i].text);
    c26_puts("READY\n");
}

static int parse_filename(const char *line, size_t command_length,
                          char name[C26_FS_NAME_MAX + 1])
{
    const char *cursor = c26_skip_spaces(line + command_length);
    size_t length = 0;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
           length < C26_FS_NAME_MAX) {
        name[length++] = *cursor++;
    }
    name[length] = '\0';
    return length != 0 && *c26_skip_spaces(cursor) == '\0';
}

static size_t append_uint(char *buffer, size_t used, size_t capacity,
                          uint16_t value)
{
    char digits[5];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0 && used < capacity) buffer[used++] = digits[--count];
    return used;
}

static void save_program(const char *line)
{
    char name[C26_FS_NAME_MAX + 1];
    if (!parse_filename(line, 4, name)) {
        c26_puts("Usage: SAVE name\n");
        return;
    }
    size_t used = 0;
    for (size_t i = 0; i < program_count; i++) {
        used = append_uint(file_buffer, used, C26_FS_FILE_MAX,
                           program[i].number);
        if (used < C26_FS_FILE_MAX) file_buffer[used++] = ' ';
        size_t length = text_length(program[i].text);
        if (used + length + 1 > C26_FS_FILE_MAX) {
            c26_puts("Error: program is too large to save\n");
            return;
        }
        memcpy(file_buffer + used, program[i].text, length);
        used += length;
        file_buffer[used++] = '\n';
    }
    if (c26_fs_save(name, file_buffer, used)) {
        c26_puts("SAVED ");
        c26_puts(name);
        c26_uart_putc('\n');
        c26_desktop_invalidate();
    } else {
        c26_puts("Error: save failed\n");
    }
}

static void load_program(const char *line)
{
    char name[C26_FS_NAME_MAX + 1];
    if (!parse_filename(line, 4, name)) {
        c26_puts("Usage: LOAD name\n");
        return;
    }
    size_t size = 0;
    if (!c26_fs_load(name, file_buffer, C26_FS_FILE_MAX, &size)) {
        c26_puts("Error: load failed\n");
        return;
    }
    file_buffer[size] = '\0';
    program_count = 0;
    size_t position = 0;
    while (position < size) {
        uint32_t number = 0;
        while (position < size && file_buffer[position] >= '0' &&
               file_buffer[position] <= '9') {
            number = number * 10U + (uint32_t)(file_buffer[position++] - '0');
        }
        if (number == 0 || number > UINT16_MAX || position >= size ||
            file_buffer[position++] != ' ') {
            c26_puts("Error: invalid BASIC file\n");
            program_count = 0;
            return;
        }
        char text[BASIC_LINE_LENGTH];
        size_t length = 0;
        while (position < size && file_buffer[position] != '\n') {
            if (length + 1 < sizeof(text)) text[length++] = file_buffer[position];
            position++;
        }
        if (position < size && file_buffer[position] == '\n') position++;
        text[length] = '\0';
        if (!store_line((uint16_t)number, text)) {
            c26_puts("Error: BASIC file exceeds program capacity\n");
            program_count = 0;
            return;
        }
    }
    c26_puts("LOADED ");
    c26_puts(name);
    c26_uart_putc('\n');
    c26_desktop_invalidate();
}

static void list_files(void)
{
    c26_puts("FILES:\n");
    size_t count = c26_fs_count();
    for (size_t i = 0; i < count; i++) {
        const char *name;
        uint32_t size;
        if (c26_fs_entry(i, &name, &size)) {
            c26_puts("  ");
            c26_puts(name);
            c26_puts("  ");
            c26_put_uint(size);
            c26_puts(" BYTES\n");
        }
    }
    if (count == 0) c26_puts("  (EMPTY)\n");
}

static void process_line(const char *line)
{
    line = c26_skip_spaces(line);
    if (*line >= '0' && *line <= '9') {
        const char *cursor = line;
        uint64_t number = c26_parse_uint(&cursor);
        cursor = c26_skip_spaces(cursor);
        if (number == 0 || number > UINT16_MAX) {
            c26_puts("Error: line number must be 1-65535\n");
            return;
        }
        int result = store_line((uint16_t)number, cursor);
        if (result == 1) c26_puts("STORED\n");
        else if (result == 2) c26_puts("DELETED\n");
        else c26_puts("Error: program is full\n");
    } else if (keyword(line, "LIST")) list_program();
    else if (keyword(line, "RUN")) run_program();
    else if (keyword(line, "NEW")) {
        program_count = 0;
        c26_puts("NEW PROGRAM\n");
        c26_desktop_invalidate();
    } else if (keyword(line, "SAVE")) save_program(line);
    else if (keyword(line, "LOAD")) load_program(line);
    else if (keyword(line, "DIR") || keyword(line, "FILES")) list_files();
    else if (keyword(line, "HELP"))
        c26_puts("PRINT LET DEVICE READ DEVICE WRITE PEEK POKE LIST RUN NEW DIR SAVE LOAD HELP\n");
    else run_statement(line);
}

static void prompt(void)
{
    c26_puts("] ");
}

void c26_basic_demo(void)
{
    static const struct {
        uint16_t number;
        const char *text;
    } startup[] = {
        {10, "LET A=26"},
        {20, "PRINT A"},
        {30, "PRINT A+16"},
        {40, "PRINT \"BASIC READY\""},
        {50, "PRINT \"HELLO FROM C26\""},
        {60, "PRINT 20+6"},
        {70, "DEVICE WRITE 128 26"},
        {80, "DEVICE READ 128"},
    };
    c26_puts("C26 BASIC V2.0 - STORED PROGRAMS + C26FS\n");
    program_count = 0;
    for (size_t i = 0; i < sizeof(startup) / sizeof(startup[0]); i++)
        store_line(startup[i].number, startup[i].text);
    run_program();
    c26_puts("BASIC INTERACTIVE READY - TYPE HELP\n");
    input_length = 0;
    prompt();
}

void c26_basic_feed_char(char ch)
{
    if (ch == '\r' || ch == '\n') {
        c26_uart_putc('\n');
        input_line[input_length] = '\0';
        process_line(input_line);
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
    if (ch < 32 || ch > 126 || input_length + 1 >= sizeof(input_line)) return;
    if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
    input_line[input_length++] = ch;
    c26_uart_putc(ch);
}
