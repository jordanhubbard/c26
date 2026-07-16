#ifndef C26_INPUT_H
#define C26_INPUT_H

#include <stdint.h>

enum {
    C26_INPUT_EVENT_KEY = 1,
    C26_INPUT_EVENT_RELATIVE = 2,
    C26_INPUT_EVENT_ABSOLUTE = 3,
};

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} c26_input_event_t;

unsigned int c26_input_init(void);
int c26_input_poll(c26_input_event_t *event);
char c26_input_key_to_ascii(uint16_t code, int shift);

#endif
