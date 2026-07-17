/* Freestanding runtime for cartridges: the compiler may synthesize calls
 * to these for aggregate initialization and copies. */

#include <stddef.h>
#include <stdint.h>

void *memset(void *destination, int value, size_t length)
{
    uint8_t *out = destination;
    for (size_t i = 0; i < length; i++) {
        out[i] = (uint8_t)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0; i < length; i++) {
        out[i] = in[i];
    }
    return destination;
}
