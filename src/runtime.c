#include "c26.h"

void *memcpy(void *destination, const void *source, size_t length)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0; i < length; i++) {
        out[i] = in[i];
    }
    return destination;
}

void *memset(void *destination, int value, size_t length)
{
    uint8_t *out = destination;
    for (size_t i = 0; i < length; i++) {
        out[i] = (uint8_t)value;
    }
    return destination;
}

int c26_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

const char *c26_skip_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

uint64_t c26_parse_uint(const char **cursor)
{
    const char *text = c26_skip_spaces(*cursor);
    uint64_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = (value * 10) + (uint64_t)(*text - '0');
        text++;
    }
    *cursor = text;
    return value;
}
