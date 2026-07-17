#include "c26.h"
#include "c26_audio.h"
#include "c26_console.h"
#include "c26_devices.h"
#include "c26_fs.h"
#include "c26_graphics.h"

#define BASIC_LINE_COUNT 256U
#define BASIC_LINE_LENGTH 80U
#define KEY_QUEUE_SIZE 256U
#define FOR_STACK_DEPTH 8U
#define GOSUB_STACK_DEPTH 8U
#define FLUSH_INTERVAL_TICKS 5U

#define ACT_NEXT 0
#define ACT_JUMP_LINE 1
#define ACT_JUMP_PC 2
#define ACT_END 3
#define ACT_ERROR 4

typedef struct {
    uint16_t number;
    char text[BASIC_LINE_LENGTH];
} basic_line_t;

typedef struct {
    int action;
    uint16_t line;
    size_t pc;
    const char *error;
} exec_t;

typedef struct {
    const char *cursor;
    const char *error;
} parser_t;

typedef struct {
    int var;
    int64_t limit;
    int64_t step;
    size_t body_pc;
} for_frame_t;

static int64_t vars[26];
static char input_line[96];
static size_t input_length;
static size_t input_cursor;
static char history[4][96];
static int history_count;
static int history_browse; /* -1 = not browsing */
static int escape_state;   /* serial ANSI arrows: 0 none, 1 ESC, 2 ESC[ */
static basic_line_t program[BASIC_LINE_COUNT];
static size_t program_count;
static char file_buffer[C26_FS_FILE_MAX + 1];

static volatile int running;
static volatile int break_flag;
static volatile int external_consumer;
static char key_queue[KEY_QUEUE_SIZE];
static unsigned int key_head;
static unsigned int key_tail;

static for_frame_t for_stack[FOR_STACK_DEPTH];
static unsigned int for_depth;
static size_t gosub_stack[GOSUB_STACK_DEPTH];
static unsigned int gosub_depth;

static uint32_t draw_color = 0xffffff;
static int gfx_dirty;
static uint64_t last_flush_tick;
static uint64_t rng_state;

static const uint32_t palette[16] = {
    0x000000, 0xffffff, 0x883932, 0x67b6bd, 0x8b3f96, 0x55a049,
    0x40318d, 0xbfce72, 0x8b5429, 0x574200, 0xb86962, 0x505050,
    0x787878, 0x94e089, 0x7869c4, 0x9f9f9f,
};

/* ------------------------------------------------------------------ */
/* Small utilities                                                     */

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') length++;
    return length;
}

static int is_upper(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static int keyword(const char *line, const char *word)
{
    while (*word != '\0' && *line == *word) {
        line++;
        word++;
    }
    return *word == '\0' && !is_upper(*line);
}

static void queue_push(char ch)
{
    if (key_head - key_tail < KEY_QUEUE_SIZE) {
        key_queue[key_head % KEY_QUEUE_SIZE] = ch;
        key_head++;
    }
}

static int queue_pop(char *ch)
{
    if (key_tail == key_head) {
        return 0;
    }
    *ch = key_queue[key_tail % KEY_QUEUE_SIZE];
    key_tail++;
    return 1;
}

static int64_t basic_random(int64_t bound)
{
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    if (bound <= 0) {
        return 0;
    }
    return (int64_t)((rng_state >> 33) % (uint64_t)bound);
}

static int64_t fb_checksum(void)
{
    const uint32_t *pixels = c26_framebuffer_pixels();
    uint64_t sum = 1469598103934665603ULL;
    for (unsigned int i = 0; i < C26_SCREEN_WIDTH * C26_SCREEN_HEIGHT; i++) {
        sum ^= pixels[i];
        sum *= 1099511628211ULL;
    }
    return (int64_t)(sum & 0xffffffffffffULL);
}

static void gfx_touch(void)
{
    gfx_dirty = 1;
    if (!running) {
        c26_framebuffer_present();
        gfx_dirty = 0;
    }
}

static void run_flush(void)
{
    uint64_t now = c26_interrupt_ticks();
    if (now - last_flush_tick < FLUSH_INTERVAL_TICKS) {
        return;
    }
    last_flush_tick = now;
    if (gfx_dirty && c26_screen_mode() == C26_SCREEN_GFX) {
        c26_framebuffer_present();
        gfx_dirty = 0;
    }
    c26_console_flush();
}

/* ------------------------------------------------------------------ */
/* Expression parser: OR -> AND -> compare -> add -> mul -> factor     */

static void set_error(parser_t *p, const char *message)
{
    if (p->error == 0) {
        p->error = message;
    }
}

static int match_char(parser_t *p, char ch)
{
    const char *cursor = c26_skip_spaces(p->cursor);
    if (*cursor != ch) {
        return 0;
    }
    p->cursor = cursor + 1;
    return 1;
}

static int match_word(parser_t *p, const char *word)
{
    const char *cursor = c26_skip_spaces(p->cursor);
    size_t i = 0;
    while (word[i] != '\0') {
        if (cursor[i] != word[i]) {
            return 0;
        }
        i++;
    }
    if (is_upper(cursor[i])) {
        return 0;
    }
    p->cursor = cursor + i;
    return 1;
}

static int64_t parse_expr(parser_t *p);

static int64_t parse_factor(parser_t *p)
{
    if (p->error != 0) {
        return 0;
    }
    if (match_char(p, '(')) {
        int64_t value = parse_expr(p);
        if (!match_char(p, ')')) {
            set_error(p, "SYNTAX");
        }
        return value;
    }
    if (match_char(p, '-')) {
        return -parse_factor(p);
    }
    if (match_word(p, "NOT")) {
        return ~parse_factor(p);
    }
    if (match_word(p, "RND") || match_word(p, "ABS") || match_word(p, "PEEK")) {
        const char *name = p->cursor;
        char kind = *(name - 1) == 'D' ? 'R' : (*(name - 1) == 'S' ? 'A' : 'P');
        if (!match_char(p, '(')) {
            set_error(p, "SYNTAX");
            return 0;
        }
        int64_t argument = parse_expr(p);
        if (!match_char(p, ')')) {
            set_error(p, "SYNTAX");
            return 0;
        }
        if (kind == 'R') {
            return basic_random(argument);
        }
        if (kind == 'A') {
            return argument < 0 ? -argument : argument;
        }
        uint8_t value = 0;
        if (!c26_device_read8((uint16_t)(argument & 0xff), &value)) {
            set_error(p, "ILLEGAL QUANTITY");
            return 0;
        }
        return value;
    }
    if (match_word(p, "TI")) {
        return (int64_t)c26_interrupt_ticks();
    }
    if (match_word(p, "FB")) {
        return fb_checksum();
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    if (*cursor >= '0' && *cursor <= '9') {
        uint64_t value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10U + (uint64_t)(*cursor - '0');
            cursor++;
        }
        p->cursor = cursor;
        return (int64_t)value;
    }
    if (is_upper(*cursor) && !is_upper(cursor[1])) {
        p->cursor = cursor + 1;
        return vars[*cursor - 'A'];
    }
    set_error(p, "SYNTAX");
    return 0;
}

static int64_t parse_mul(parser_t *p)
{
    int64_t value = parse_factor(p);
    for (;;) {
        if (match_char(p, '*')) {
            value *= parse_factor(p);
        } else if (match_char(p, '/')) {
            int64_t divisor = parse_factor(p);
            if (divisor == 0) {
                set_error(p, "DIVISION BY ZERO");
                return 0;
            }
            value /= divisor;
        } else if (match_word(p, "MOD")) {
            int64_t divisor = parse_factor(p);
            if (divisor == 0) {
                set_error(p, "DIVISION BY ZERO");
                return 0;
            }
            value %= divisor;
        } else {
            return value;
        }
    }
}

static int64_t parse_add(parser_t *p)
{
    int64_t value = parse_mul(p);
    for (;;) {
        if (match_char(p, '+')) {
            value += parse_mul(p);
        } else if (match_char(p, '-')) {
            value -= parse_mul(p);
        } else {
            return value;
        }
    }
}

static int64_t parse_compare(parser_t *p)
{
    int64_t left = parse_add(p);
    const char *cursor = c26_skip_spaces(p->cursor);
    int64_t truth;
    if (cursor[0] == '<' && cursor[1] == '>') {
        p->cursor = cursor + 2;
        truth = left != parse_add(p);
    } else if (cursor[0] == '<' && cursor[1] == '=') {
        p->cursor = cursor + 2;
        truth = left <= parse_add(p);
    } else if (cursor[0] == '>' && cursor[1] == '=') {
        p->cursor = cursor + 2;
        truth = left >= parse_add(p);
    } else if (cursor[0] == '<') {
        p->cursor = cursor + 1;
        truth = left < parse_add(p);
    } else if (cursor[0] == '>') {
        p->cursor = cursor + 1;
        truth = left > parse_add(p);
    } else if (cursor[0] == '=') {
        p->cursor = cursor + 1;
        truth = left == parse_add(p);
    } else {
        return left;
    }
    return truth ? -1 : 0;
}

static int64_t parse_and(parser_t *p)
{
    int64_t value = parse_compare(p);
    while (match_word(p, "AND")) {
        value &= parse_compare(p);
    }
    return value;
}

static int64_t parse_expr(parser_t *p)
{
    int64_t value = parse_and(p);
    while (match_word(p, "OR")) {
        value |= parse_and(p);
    }
    return value;
}

static int parse_end(parser_t *p)
{
    return *c26_skip_spaces(p->cursor) == '\0';
}

/* ------------------------------------------------------------------ */
/* Program storage                                                     */

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

static int find_line(uint16_t number)
{
    for (size_t i = 0; i < program_count; i++) {
        if (program[i].number == number) {
            return (int)i;
        }
    }
    return -1;
}

static void list_program(void)
{
    for (size_t i = 0; i < program_count; i++) {
        c26_put_uint(program[i].number);
        c26_putc(' ');
        c26_puts(program[i].text);
        c26_putc('\n');
    }
}

/* ------------------------------------------------------------------ */
/* Blocking interaction primitives (program mode only)                 */

static int input_read_line(char *buffer, size_t capacity)
{
    size_t length = 0;
    for (;;) {
        c26_io_pump();
        char ch;
        while (queue_pop(&ch)) {
            if (ch == '\n') {
                c26_putc('\n');
                buffer[length] = '\0';
                return 1;
            }
            if (ch == '\b') {
                if (length != 0) {
                    length--;
                    c26_puts("\b \b");
                }
                continue;
            }
            if (ch < 32 || ch > 126 || length + 1 >= capacity) {
                continue;
            }
            buffer[length++] = ch;
            c26_putc(ch);
        }
        if (break_flag) {
            return 0;
        }
        run_flush();
        c26_idle();
    }
}

/* ------------------------------------------------------------------ */
/* Statement execution                                                 */

static exec_t ok_next(void)
{
    exec_t result = {ACT_NEXT, 0, 0, 0};
    return result;
}

static exec_t fail(const char *message)
{
    exec_t result = {ACT_ERROR, 0, 0, message};
    return result;
}

static exec_t exec_statement(const char *text, long pc);

static exec_t exec_print(parser_t *p)
{
    if (parse_end(p)) {
        c26_putc('\n');
        return ok_next();
    }
    for (;;) {
        const char *cursor = c26_skip_spaces(p->cursor);
        if (*cursor == '"') {
            cursor++;
            while (*cursor != '\0' && *cursor != '"') {
                c26_putc(*cursor++);
            }
            if (*cursor != '"') {
                return fail("SYNTAX");
            }
            p->cursor = cursor + 1;
        } else {
            int64_t value = parse_expr(p);
            if (p->error != 0) {
                return fail(p->error);
            }
            c26_put_int(value);
        }
        if (match_char(p, ';')) {
            if (parse_end(p)) {
                return ok_next();
            }
            continue;
        }
        if (match_char(p, ',')) {
            c26_puts("  ");
            if (parse_end(p)) {
                return ok_next();
            }
            continue;
        }
        if (parse_end(p)) {
            c26_putc('\n');
            return ok_next();
        }
        return fail("SYNTAX");
    }
}

static exec_t exec_assign(parser_t *p)
{
    const char *cursor = c26_skip_spaces(p->cursor);
    if (!is_upper(*cursor) || is_upper(cursor[1])) {
        return fail("SYNTAX");
    }
    int index = *cursor - 'A';
    p->cursor = cursor + 1;
    if (!match_char(p, '=')) {
        return fail("SYNTAX");
    }
    int64_t value = parse_expr(p);
    if (p->error != 0 || !parse_end(p)) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    vars[index] = value;
    return ok_next();
}

static exec_t exec_if(parser_t *p, long pc)
{
    int64_t condition = parse_expr(p);
    if (p->error != 0) {
        return fail(p->error);
    }
    int has_then = match_word(p, "THEN");
    int has_goto = has_then ? 0 : match_word(p, "GOTO");
    if (!has_then && !has_goto) {
        return fail("SYNTAX");
    }
    const char *rest = c26_skip_spaces(p->cursor);
    if (condition == 0) {
        return ok_next();
    }
    if (*rest >= '0' && *rest <= '9') {
        parser_t q = {rest, 0};
        int64_t target = parse_expr(&q);
        if (q.error != 0 || !parse_end(&q) || target <= 0 ||
            target > UINT16_MAX) {
            return fail("SYNTAX");
        }
        exec_t result = {ACT_JUMP_LINE, (uint16_t)target, 0, 0};
        return result;
    }
    if (has_goto) {
        return fail("SYNTAX");
    }
    return exec_statement(rest, pc);
}

static exec_t exec_for(parser_t *p, long pc)
{
    if (pc < 0) {
        return fail("ILLEGAL DIRECT");
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    if (!is_upper(*cursor) || is_upper(cursor[1])) {
        return fail("SYNTAX");
    }
    int index = *cursor - 'A';
    p->cursor = cursor + 1;
    if (!match_char(p, '=')) {
        return fail("SYNTAX");
    }
    int64_t start = parse_expr(p);
    if (p->error != 0 || !match_word(p, "TO")) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    int64_t limit = parse_expr(p);
    int64_t step = 1;
    if (match_word(p, "STEP")) {
        step = parse_expr(p);
    }
    if (p->error != 0 || !parse_end(p)) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    if (for_depth == FOR_STACK_DEPTH) {
        return fail("FOR STACK OVERFLOW");
    }
    vars[index] = start;
    for_stack[for_depth].var = index;
    for_stack[for_depth].limit = limit;
    for_stack[for_depth].step = step;
    for_stack[for_depth].body_pc = (size_t)pc + 1;
    for_depth++;
    return ok_next();
}

static exec_t exec_next(parser_t *p, long pc)
{
    if (pc < 0) {
        return fail("ILLEGAL DIRECT");
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    int wanted = -1;
    if (is_upper(*cursor) && !is_upper(cursor[1])) {
        wanted = *cursor - 'A';
        p->cursor = cursor + 1;
    }
    if (!parse_end(p)) {
        return fail("SYNTAX");
    }
    if (wanted >= 0) {
        while (for_depth != 0 && for_stack[for_depth - 1].var != wanted) {
            for_depth--;
        }
    }
    if (for_depth == 0) {
        return fail("NEXT WITHOUT FOR");
    }
    for_frame_t *frame = &for_stack[for_depth - 1];
    vars[frame->var] += frame->step;
    int continue_loop = frame->step >= 0 ? vars[frame->var] <= frame->limit
                                         : vars[frame->var] >= frame->limit;
    if (continue_loop) {
        exec_t result = {ACT_JUMP_PC, 0, frame->body_pc, 0};
        return result;
    }
    for_depth--;
    return ok_next();
}

static exec_t exec_input(parser_t *p, long pc)
{
    if (pc < 0) {
        return fail("ILLEGAL DIRECT");
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    if (!is_upper(*cursor) || is_upper(cursor[1])) {
        return fail("SYNTAX");
    }
    int index = *cursor - 'A';
    p->cursor = cursor + 1;
    if (!parse_end(p)) {
        return fail("SYNTAX");
    }
    char buffer[32];
    for (;;) {
        c26_puts("? ");
        run_flush();
        if (!input_read_line(buffer, sizeof(buffer))) {
            return ok_next();
        }
        const char *scan = c26_skip_spaces(buffer);
        int negative = 0;
        if (*scan == '-') {
            negative = 1;
            scan++;
        }
        if (*scan < '0' || *scan > '9') {
            c26_puts("?REDO\n");
            continue;
        }
        int64_t value = 0;
        while (*scan >= '0' && *scan <= '9') {
            value = value * 10 + (*scan - '0');
            scan++;
        }
        if (*c26_skip_spaces(scan) != '\0') {
            c26_puts("?REDO\n");
            continue;
        }
        vars[index] = negative ? -value : value;
        return ok_next();
    }
}

static exec_t exec_get(parser_t *p, long pc)
{
    if (pc < 0) {
        return fail("ILLEGAL DIRECT");
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    if (!is_upper(*cursor) || is_upper(cursor[1])) {
        return fail("SYNTAX");
    }
    int index = *cursor - 'A';
    p->cursor = cursor + 1;
    if (!parse_end(p)) {
        return fail("SYNTAX");
    }
    c26_io_pump();
    char ch;
    vars[index] = queue_pop(&ch) ? (int64_t)(uint8_t)ch : 0;
    return ok_next();
}

static exec_t exec_pause(parser_t *p, long pc)
{
    if (pc < 0) {
        return fail("ILLEGAL DIRECT");
    }
    int64_t duration = parse_expr(p);
    if (p->error != 0 || !parse_end(p)) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    if (duration < 0) {
        return fail("ILLEGAL QUANTITY");
    }
    uint64_t target = c26_interrupt_ticks() + (uint64_t)duration;
    while (c26_interrupt_ticks() < target && !break_flag) {
        c26_io_pump();
        run_flush();
        c26_idle();
    }
    return ok_next();
}

static uint32_t resolve_color(int64_t value)
{
    if (value >= 0 && value < 16) {
        return palette[value];
    }
    return (uint32_t)(value & 0xffffff);
}

static exec_t exec_screen(parser_t *p)
{
    int64_t mode = parse_expr(p);
    if (p->error != 0 || !parse_end(p)) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    if (mode == 0) {
        c26_screen_set_mode(C26_SCREEN_CONSOLE);
        c26_console_flush();
        return ok_next();
    }
    if (mode == 1) {
        c26_screen_set_mode(C26_SCREEN_GFX);
        c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, (int)C26_SCREEN_HEIGHT, 0);
        c26_framebuffer_present();
        gfx_dirty = 0;
        return ok_next();
    }
    return fail("ILLEGAL QUANTITY");
}

static exec_t exec_cls(parser_t *p)
{
    if (!parse_end(p)) {
        return fail("SYNTAX");
    }
    if (c26_screen_mode() == C26_SCREEN_GFX) {
        c26_fill_rect(0, 0, (int)C26_SCREEN_WIDTH, (int)C26_SCREEN_HEIGHT, 0);
        gfx_touch();
    } else {
        c26_console_clear();
        c26_console_flush();
    }
    return ok_next();
}

static int parse_arguments(parser_t *p, int64_t *values, int minimum,
                           int maximum)
{
    int count = 0;
    while (count < maximum) {
        values[count++] = parse_expr(p);
        if (p->error != 0) {
            return -1;
        }
        if (!match_char(p, ',')) {
            break;
        }
    }
    if (count < minimum || !parse_end(p)) {
        return -1;
    }
    return count;
}

static exec_t exec_color(parser_t *p)
{
    int64_t values[1];
    if (parse_arguments(p, values, 1, 1) < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    draw_color = resolve_color(values[0]);
    return ok_next();
}

static exec_t exec_plot(parser_t *p)
{
    int64_t values[2];
    if (parse_arguments(p, values, 2, 2) < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    c26_draw_pixel((int)values[0], (int)values[1], draw_color);
    gfx_touch();
    return ok_next();
}

static exec_t exec_line(parser_t *p)
{
    int64_t values[4];
    if (parse_arguments(p, values, 4, 4) < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    c26_draw_line((int)values[0], (int)values[1], (int)values[2],
                  (int)values[3], draw_color);
    gfx_touch();
    return ok_next();
}

static exec_t exec_rect(parser_t *p)
{
    int64_t values[5];
    int count = parse_arguments(p, values, 4, 5);
    if (count < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    if (count == 5 && values[4] != 0) {
        c26_fill_rect((int)values[0], (int)values[1], (int)values[2],
                      (int)values[3], draw_color);
    } else {
        c26_draw_rect((int)values[0], (int)values[1], (int)values[2],
                      (int)values[3], draw_color);
    }
    gfx_touch();
    return ok_next();
}

static exec_t exec_text(parser_t *p)
{
    int64_t x = parse_expr(p);
    if (p->error != 0 || !match_char(p, ',')) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    int64_t y = parse_expr(p);
    if (p->error != 0 || !match_char(p, ',')) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    const char *cursor = c26_skip_spaces(p->cursor);
    if (*cursor != '"') {
        return fail("SYNTAX");
    }
    cursor++;
    char message[64];
    size_t length = 0;
    while (*cursor != '\0' && *cursor != '"' && length + 1 < sizeof(message)) {
        message[length++] = *cursor++;
    }
    message[length] = '\0';
    if (*cursor != '"') {
        return fail("SYNTAX");
    }
    parser_t q = {cursor + 1, 0};
    if (!parse_end(&q)) {
        return fail("SYNTAX");
    }
    c26_draw_text((int)x, (int)y, message, draw_color, 0x000000, 2);
    gfx_touch();
    return ok_next();
}

static exec_t exec_sound(parser_t *p)
{
    int64_t values[4];
    int count = parse_arguments(p, values, 2, 4);
    if (count < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    if (values[0] < 0 || values[0] >= (int64_t)C26_AUDIO_VOICE_COUNT) {
        return fail("ILLEGAL QUANTITY");
    }
    if (values[1] < 0) {
        return fail("ILLEGAL QUANTITY");
    }
    unsigned int voice = (unsigned int)values[0];
    if (values[1] == 0) {
        c26_audio_voice_stop(voice);
        return ok_next();
    }
    c26_waveform_t waveform = C26_WAVE_SQUARE;
    if (count >= 3) {
        if (values[2] < 0 || values[2] > 3) {
            return fail("ILLEGAL QUANTITY");
        }
        waveform = (c26_waveform_t)values[2];
    }
    uint8_t volume = 200;
    if (count == 4) {
        if (values[3] < 0 || values[3] > 255) {
            return fail("ILLEGAL QUANTITY");
        }
        volume = (uint8_t)values[3];
    }
    if (!c26_audio_voice_start(voice, waveform, (uint32_t)values[1], volume,
                               128)) {
        return fail("ILLEGAL QUANTITY");
    }
    return ok_next();
}

/* DEVICE and POKE historically take space-separated operands; the comma
   stays optional here because registers are unsigned (no '-' ambiguity). */
static int parse_register_pair(parser_t *p, int64_t *reg, int64_t *value)
{
    *reg = parse_expr(p);
    if (p->error != 0) {
        return 0;
    }
    (void)match_char(p, ',');
    *value = parse_expr(p);
    return p->error == 0 && parse_end(p);
}

static exec_t exec_device(parser_t *p)
{
    if (match_word(p, "WRITE")) {
        int64_t reg;
        int64_t value;
        if (!parse_register_pair(p, &reg, &value)) {
            return fail(p->error != 0 ? p->error : "SYNTAX");
        }
        if (!c26_device_write8((uint16_t)reg, (uint8_t)value)) {
            return fail("ILLEGAL QUANTITY");
        }
        c26_puts("DEVICE WRITE OK\n");
        return ok_next();
    }
    if (match_word(p, "READ")) {
        int64_t values[1];
        if (parse_arguments(p, values, 1, 1) < 0) {
            return fail(p->error != 0 ? p->error : "SYNTAX");
        }
        uint8_t value = 0;
        if (!c26_device_read8((uint16_t)values[0], &value)) {
            return fail("ILLEGAL QUANTITY");
        }
        c26_puts("DEVICE READ returned ");
        c26_put_uint(value);
        c26_putc('\n');
        return ok_next();
    }
    return fail("SYNTAX");
}

static exec_t exec_poke(parser_t *p)
{
    int64_t reg;
    int64_t value;
    if (!parse_register_pair(p, &reg, &value)) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    c26_device_write8((uint16_t)(reg & 0xff), (uint8_t)value);
    c26_puts("POKE compatibility alias used DEVICE WRITE\n");
    return ok_next();
}

static exec_t exec_peek_statement(parser_t *p)
{
    int64_t values[1];
    if (parse_arguments(p, values, 1, 1) < 0) {
        return fail(p->error != 0 ? p->error : "SYNTAX");
    }
    uint8_t value = 0;
    c26_device_read8((uint16_t)(values[0] & 0xff), &value);
    c26_puts("PEEK compatibility alias returned ");
    c26_put_uint(value);
    c26_putc('\n');
    return ok_next();
}

static exec_t exec_statement(const char *text, long pc)
{
    const char *line = c26_skip_spaces(text);
    parser_t p = {line, 0};
    if (*line == '\0' || keyword(line, "REM")) {
        return ok_next();
    }
    if (keyword(line, "PRINT")) {
        p.cursor = line + 5;
        return exec_print(&p);
    }
    if (keyword(line, "INPUT")) {
        p.cursor = line + 5;
        return exec_input(&p, pc);
    }
    if (keyword(line, "GET")) {
        p.cursor = line + 3;
        return exec_get(&p, pc);
    }
    if (keyword(line, "IF")) {
        p.cursor = line + 2;
        return exec_if(&p, pc);
    }
    if (keyword(line, "GOTO")) {
        if (pc < 0) {
            return fail("ILLEGAL DIRECT");
        }
        p.cursor = line + 4;
        int64_t target = parse_expr(&p);
        if (p.error != 0 || !parse_end(&p) || target <= 0 ||
            target > UINT16_MAX) {
            return fail(p.error != 0 ? p.error : "SYNTAX");
        }
        exec_t result = {ACT_JUMP_LINE, (uint16_t)target, 0, 0};
        return result;
    }
    if (keyword(line, "GOSUB")) {
        if (pc < 0) {
            return fail("ILLEGAL DIRECT");
        }
        p.cursor = line + 5;
        int64_t target = parse_expr(&p);
        if (p.error != 0 || !parse_end(&p) || target <= 0 ||
            target > UINT16_MAX) {
            return fail(p.error != 0 ? p.error : "SYNTAX");
        }
        if (gosub_depth == GOSUB_STACK_DEPTH) {
            return fail("GOSUB STACK OVERFLOW");
        }
        gosub_stack[gosub_depth++] = (size_t)pc + 1;
        exec_t result = {ACT_JUMP_LINE, (uint16_t)target, 0, 0};
        return result;
    }
    if (keyword(line, "RETURN")) {
        if (pc < 0) {
            return fail("ILLEGAL DIRECT");
        }
        if (gosub_depth == 0) {
            return fail("RETURN WITHOUT GOSUB");
        }
        exec_t result = {ACT_JUMP_PC, 0, gosub_stack[--gosub_depth], 0};
        return result;
    }
    if (keyword(line, "FOR")) {
        p.cursor = line + 3;
        return exec_for(&p, pc);
    }
    if (keyword(line, "NEXT")) {
        p.cursor = line + 4;
        return exec_next(&p, pc);
    }
    if (keyword(line, "END") || keyword(line, "STOP")) {
        exec_t result = {ACT_END, 0, 0, 0};
        return result;
    }
    if (keyword(line, "PAUSE")) {
        p.cursor = line + 5;
        return exec_pause(&p, pc);
    }
    if (keyword(line, "SCREEN")) {
        p.cursor = line + 6;
        return exec_screen(&p);
    }
    if (keyword(line, "CLS")) {
        p.cursor = line + 3;
        return exec_cls(&p);
    }
    if (keyword(line, "COLOR")) {
        p.cursor = line + 5;
        return exec_color(&p);
    }
    if (keyword(line, "PLOT")) {
        p.cursor = line + 4;
        return exec_plot(&p);
    }
    if (keyword(line, "LINE")) {
        p.cursor = line + 4;
        return exec_line(&p);
    }
    if (keyword(line, "RECT")) {
        p.cursor = line + 4;
        return exec_rect(&p);
    }
    if (keyword(line, "TEXT")) {
        p.cursor = line + 4;
        return exec_text(&p);
    }
    if (keyword(line, "SOUND")) {
        p.cursor = line + 5;
        return exec_sound(&p);
    }
    if (keyword(line, "DEVICE")) {
        p.cursor = line + 6;
        return exec_device(&p);
    }
    if (keyword(line, "POKE")) {
        p.cursor = line + 4;
        return exec_poke(&p);
    }
    if (keyword(line, "PEEK")) {
        p.cursor = line + 4;
        return exec_peek_statement(&p);
    }
    if (keyword(line, "ROBOT")) {
        c26_robot_demo();
        return ok_next();
    }
    if (keyword(line, "LET")) {
        p.cursor = line + 3;
        return exec_assign(&p);
    }
    if (is_upper(line[0]) && !is_upper(line[1])) {
        p.cursor = line;
        return exec_assign(&p);
    }
    return fail("SYNTAX");
}

/* ------------------------------------------------------------------ */
/* Run engine                                                          */

static void run_program(void)
{
    running = 1;
    break_flag = 0;
    for_depth = 0;
    gosub_depth = 0;
    size_t pc = 0;
    const char *error = 0;
    uint16_t error_line = 0;
    while (pc < program_count) {
        if (break_flag) {
            c26_puts("BREAK IN ");
            c26_put_uint(program[pc].number);
            c26_putc('\n');
            break;
        }
        uint16_t line_number = program[pc].number;
        exec_t result = exec_statement(program[pc].text, (long)pc);
        if (result.action == ACT_ERROR) {
            error = result.error;
            error_line = line_number;
            break;
        }
        if (result.action == ACT_END) {
            break;
        }
        if (result.action == ACT_JUMP_LINE) {
            int index = find_line(result.line);
            if (index < 0) {
                error = "UNDEFINED LINE";
                error_line = line_number;
                break;
            }
            pc = (size_t)index;
        } else if (result.action == ACT_JUMP_PC) {
            pc = result.pc;
        } else {
            pc++;
        }
        c26_io_pump();
        run_flush();
    }
    if (error != 0) {
        c26_putc('?');
        c26_puts(error);
        c26_puts(" ERROR IN ");
        c26_put_uint(error_line);
        c26_putc('\n');
    }
    if (gfx_dirty && c26_screen_mode() == C26_SCREEN_GFX) {
        c26_framebuffer_present();
        gfx_dirty = 0;
    }
    running = 0;
    c26_puts("READY\n");
    /* Replay type-ahead — but stop the moment a replayed line hands the
       queue to a new consumer (RUN restarts, RUN "CART" spawns), or the
       feed would push each popped char straight back forever. */
    char ch;
    while (!running && !external_consumer && queue_pop(&ch)) {
        c26_basic_feed_char(ch);
    }
}

/* ------------------------------------------------------------------ */
/* Files                                                               */

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
        c26_putc('\n');
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
    c26_putc('\n');
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

/* ------------------------------------------------------------------ */
/* Line editor and immediate mode                                      */

static void process_line(const char *line)
{
    line = c26_skip_spaces(line);
    if (*line >= '0' && *line <= '9') {
        const char *cursor = line;
        uint64_t number = c26_parse_uint(&cursor);
        cursor = c26_skip_spaces(cursor);
        if (number == 0 || number > UINT16_MAX) {
            c26_puts("?LINE NUMBER ERROR\n");
            return;
        }
        if (store_line((uint16_t)number, cursor) == 0 && *cursor != '\0') {
            c26_puts("?PROGRAM FULL ERROR\n");
        }
        return;
    }
    if (keyword(line, "LIST")) {
        list_program();
        return;
    }
    if (keyword(line, "RUN")) {
        const char *cursor = c26_skip_spaces(line + 3);
        if (*cursor == '"' || is_upper(*cursor)) {
            /* RUN "NAME" or bare RUN NAME launches a cartridge. */
            int quoted = *cursor == '"';
            if (quoted) cursor++;
            char name[C26_FS_NAME_MAX + 1];
            size_t length = 0;
            while (*cursor != '\0' && *cursor != '"' && *cursor != ' ' &&
                   length < C26_FS_NAME_MAX) {
                name[length++] = *cursor++;
            }
            name[length] = '\0';
            if ((quoted && *cursor != '"') || length == 0) {
                c26_puts("Usage: RUN or RUN NAME\n");
                return;
            }
            c26_cart_run(name);
            return;
        }
        if (*cursor != '\0') {
            c26_puts("Usage: RUN or RUN NAME\n");
            return;
        }
        run_program();
        return;
    }
    if (keyword(line, "NEW")) {
        program_count = 0;
        c26_puts("NEW PROGRAM\n");
        c26_desktop_invalidate();
        return;
    }
    if (keyword(line, "SAVE")) {
        save_program(line);
        return;
    }
    if (keyword(line, "LOAD")) {
        load_program(line);
        return;
    }
    if (keyword(line, "DIR") || keyword(line, "FILES")) {
        list_files();
        return;
    }
    if (keyword(line, "BYE") || keyword(line, "EXIT") ||
        keyword(line, "QUIT") || keyword(line, "SHUTDOWN")) {
        c26_puts("SHUTTING DOWN - GOODBYE\n");
        c26_console_flush();
        c26_poweroff();
    }
    if (keyword(line, "HALT")) {
        /* Debug exit: no farewell, no flush — the machine stops NOW. */
        c26_poweroff();
    }
    if (keyword(line, "JOBS")) {
        c26_puts("JOBS:\n");
        c26_cart_list_jobs();
        return;
    }
    if (keyword(line, "KILL")) {
        const char *cursor = line + 4;
        uint64_t job = c26_parse_uint(&cursor);
        if (!c26_cart_kill((int)job)) {
            c26_puts("Error: no such job\n");
        }
        return;
    }
    if (keyword(line, "DELETE")) {
        char name[C26_FS_NAME_MAX + 1];
        if (!parse_filename(line, 6, name)) {
            c26_puts("Usage: DELETE name\n");
        } else if (c26_fs_delete(name)) {
            c26_puts("DELETED ");
            c26_puts(name);
            c26_putc('\n');
            c26_desktop_invalidate();
        } else {
            c26_puts("Error: delete failed\n");
        }
        return;
    }
    if (keyword(line, "RENAME")) {
        char old_name[C26_FS_NAME_MAX + 1];
        char new_name[C26_FS_NAME_MAX + 1];
        const char *cursor = c26_skip_spaces(line + 6);
        size_t length = 0;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != ',' &&
               length < C26_FS_NAME_MAX) {
            old_name[length++] = *cursor++;
        }
        old_name[length] = '\0';
        if (*cursor == ',') cursor++;
        if (!parse_filename(cursor, 0, new_name) || length == 0) {
            c26_puts("Usage: RENAME old new\n");
        } else if (c26_fs_rename(old_name, new_name)) {
            c26_puts("RENAMED ");
            c26_puts(old_name);
            c26_puts(" TO ");
            c26_puts(new_name);
            c26_putc('\n');
            c26_desktop_invalidate();
        } else {
            c26_puts("Error: rename failed\n");
        }
        return;
    }
    if (keyword(line, "HELP")) {
        c26_puts("PRINT LET INPUT GET IF THEN GOTO GOSUB RETURN FOR NEXT END REM PAUSE\n");
        c26_puts("SCREEN CLS COLOR PLOT LINE RECT TEXT SOUND DEVICE PEEK POKE ROBOT\n");
        c26_puts("LIST RUN NEW DIR SAVE LOAD DELETE RENAME RUN \"CART\" JOBS KILL BYE HELP\n");
        c26_puts("FUNCTIONS: RND ABS PEEK TI FB\n");
        return;
    }
    exec_t result = exec_statement(line, -1);
    if (result.action == ACT_ERROR) {
        c26_putc('?');
        c26_puts(result.error);
        c26_puts(" ERROR\n");
    } else if (result.action == ACT_JUMP_LINE || result.action == ACT_JUMP_PC) {
        c26_puts("?ILLEGAL DIRECT ERROR\n");
    }
}

static void prompt(void)
{
    c26_puts("] ");
}

/* ------------------------------------------------------------------ */
/* Line editor: cursor movement, mid-line insert/delete, history       */

static void editor_repaint_tail(size_t from, int trailing_blank)
{
    for (size_t i = from; i < input_length; i++) {
        c26_putc(input_line[i]);
    }
    if (trailing_blank) {
        c26_putc(' ');
        c26_putc('\b');
    }
    for (size_t i = from; i < input_length; i++) {
        c26_putc('\b');
    }
}

static void editor_insert(char ch)
{
    if (input_length + 1 >= sizeof(input_line)) {
        return;
    }
    for (size_t i = input_length; i > input_cursor; i--) {
        input_line[i] = input_line[i - 1];
    }
    input_line[input_cursor] = ch;
    input_length++;
    input_cursor++;
    c26_putc(ch);
    editor_repaint_tail(input_cursor, 0);
}

static void editor_erase(void)
{
    if (input_cursor == 0) {
        return;
    }
    for (size_t i = input_cursor; i < input_length; i++) {
        input_line[i - 1] = input_line[i];
    }
    input_cursor--;
    input_length--;
    c26_putc('\b');
    editor_repaint_tail(input_cursor, 1);
}

static void editor_set_line(const char *text)
{
    while (input_cursor < input_length) {
        c26_putc(input_line[input_cursor++]);
    }
    while (input_length != 0) {
        c26_puts("\b \b");
        input_length--;
    }
    input_cursor = 0;
    while (text[input_length] != '\0' &&
           input_length + 1 < sizeof(input_line)) {
        input_line[input_length] = text[input_length];
        c26_putc(text[input_length]);
        input_length++;
    }
    input_cursor = input_length;
}

static int lines_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static void editor_history_add(const char *text)
{
    if (text[0] == '\0' ||
        (history_count > 0 &&
         lines_equal(history[(history_count - 1) % 4], text))) {
        return;
    }
    size_t i = 0;
    while (text[i] != '\0' && i + 1 < sizeof(history[0])) {
        history[history_count % 4][i] = text[i];
        i++;
    }
    history[history_count % 4][i] = '\0';
    history_count++;
}

static void editor_history_browse(int direction)
{
    if (history_count == 0) {
        return;
    }
    int oldest = history_count > 4 ? history_count - 4 : 0;
    if (history_browse < 0) {
        if (direction > 0) return;
        history_browse = history_count - 1;
    } else {
        history_browse += direction;
    }
    if (history_browse < oldest) history_browse = oldest;
    if (history_browse >= history_count) {
        history_browse = -1;
        editor_set_line("");
        return;
    }
    editor_set_line(history[history_browse % 4]);
}

static const char demo_program[] =
    "10 REM C26 SELF DEMO\n"
    "20 SCREEN 1\n"
    "30 COLOR 14\n"
    "40 FOR I=0 TO 15\n"
    "50 RECT 20+I*18,15+I*14,600-I*36,450-I*28\n"
    "60 NEXT\n"
    "70 COLOR 1\n"
    "80 TEXT 250,225,\"C26 2026\"\n"
    "90 FOR I=0 TO 7\n"
    "100 SOUND 0,262+I*33\n"
    "110 PAUSE 10\n"
    "120 NEXT\n"
    "130 SOUND 0,0\n"
    "140 PAUSE 50\n"
    "150 SCREEN 0\n"
    "160 PRINT \"SELF DEMO COMPLETE\"\n";

void c26_basic_init(void)
{
    c26_puts("C26 BASIC V3.0 - EXPRESSIONS, CONTROL FLOW, HARDWARE STATEMENTS\n");
    c26_puts("TYPE HELP FOR COMMANDS, BYE TO POWER OFF, ESC FOR THE DESKTOP\n");
    program_count = 0;
    rng_state = c26_interrupt_ticks() * 6364136223846793005ULL + 1;
    if (c26_fs_online()) {
        size_t size = 0;
        if (!c26_fs_load("DEMO", file_buffer, C26_FS_FILE_MAX, &size)) {
            if (c26_fs_save("DEMO", demo_program, sizeof(demo_program) - 1)) {
                c26_puts("C26FS: DEMO program installed\n");
            }
        }
        c26_puts("TYPE LOAD DEMO THEN RUN FOR THE SELF DEMO\n");
    }
    c26_puts("BASIC INTERACTIVE READY - TYPE HELP\n");
    c26_puts("READY\n");
    input_length = 0;
    input_cursor = 0;
    prompt();
}

int c26_basic_running(void)
{
    return running;
}

int c26_basic_queue_consumed_externally(void)
{
    return external_consumer;
}

int c26_basic_can_accept(void)
{
    return !(running || external_consumer) ||
           key_head - key_tail < KEY_QUEUE_SIZE;
}

int c26_basic_key_pop(char *ch)
{
    return queue_pop(ch);
}

int c26_basic_break_requested(void)
{
    return break_flag;
}

void c26_basic_clear_break(void)
{
    break_flag = 0;
}

void c26_basic_set_external_consumer(int on)
{
    external_consumer = on != 0;
}

void c26_basic_feed_char(char ch)
{
    if (ch == '\r') {
        ch = '\n';
    }
    if (ch == 0x14) { /* Ctrl-T: focus the console, apps keep running */
        c26_cart_focus_console();
        return;
    }
    if (running || external_consumer) {
        /* Ctrl-C always breaks; Esc breaks BASIC but is delivered to a
           cartridge so apps can use it themselves. */
        if (ch == 0x03 || (ch == 0x1b && !external_consumer)) {
            break_flag = 1;
            return;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch -= ('a' - 'A');
        }
        queue_push(ch);
        return;
    }
    /* Serial terminals send arrows as ESC [ A/B/C/D; fold them into the
       same codes the virtio keyboard delivers. */
    if (escape_state == 1) {
        escape_state = ch == '[' ? 2 : 0;
        if (escape_state != 0 || ch == 0x1b) return;
    } else if (escape_state == 2) {
        escape_state = 0;
        if (ch == 'A') ch = 0x1c;
        else if (ch == 'B') ch = 0x1d;
        else if (ch == 'C') ch = 0x1e;
        else if (ch == 'D') ch = 0x1f;
        else return;
    } else if (ch == 0x1b) {
        escape_state = 1;
        return;
    }

    if (ch == '\n') {
        c26_putc('\n');
        input_line[input_length] = '\0';
        editor_history_add(input_line);
        history_browse = -1;
        input_length = 0;
        input_cursor = 0;
        process_line(input_line);
        prompt();
        return;
    }
    if (ch == '\b' || ch == 0x7f) {
        editor_erase();
        return;
    }
    if (ch == 0x1c) { /* up: previous history entry */
        editor_history_browse(-1);
        return;
    }
    if (ch == 0x1d) { /* down: next history entry */
        editor_history_browse(1);
        return;
    }
    if (ch == 0x1f) { /* left */
        if (input_cursor != 0) {
            input_cursor--;
            c26_putc('\b');
        }
        return;
    }
    if (ch == 0x1e) { /* right */
        if (input_cursor < input_length) {
            c26_putc(input_line[input_cursor]);
            input_cursor++;
        }
        return;
    }
    if (ch < 32 || ch > 126) return;
    if (ch >= 'a' && ch <= 'z') ch -= ('a' - 'A');
    editor_insert(ch);
}
