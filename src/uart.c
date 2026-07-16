#include "c26.h"

#define UART0_BASE 0x10000000UL
#define UART_THR 0
#define UART_RBR 0
#define UART_IER 1
#define UART_LSR 5
#define UART_LSR_DR 0x01
#define UART_LSR_THRE 0x20

static volatile uint8_t *const uart0 = (volatile uint8_t *)UART0_BASE;
static volatile uint8_t receive_buffer[256];
static volatile uint16_t receive_head;
static volatile uint16_t receive_tail;

void c26_uart_putc(char ch)
{
    if (ch == '\n') {
        c26_uart_putc('\r');
    }
    while ((uart0[UART_LSR] & UART_LSR_THRE) == 0) {
    }
    uart0[UART_THR] = (uint8_t)ch;
}

int c26_uart_getc_nonblocking(void)
{
    if (receive_tail != receive_head) {
        uint8_t value = receive_buffer[receive_tail & 0xffU];
        receive_tail++;
        return value;
    }
    if ((uart0[UART_LSR] & UART_LSR_DR) != 0) {
        return uart0[UART_RBR];
    }
    return -1;
}

void c26_uart_enable_interrupt(void)
{
    uart0[UART_IER] = 1;
}

void c26_uart_handle_interrupt(void)
{
    while ((uart0[UART_LSR] & UART_LSR_DR) != 0) {
        uint8_t value = uart0[UART_RBR];
        uint16_t next = (uint16_t)(receive_head + 1);
        if ((uint16_t)(next - receive_tail) <= sizeof(receive_buffer)) {
            receive_buffer[receive_head & 0xffU] = value;
            receive_head = next;
        }
    }
}

void c26_puts(const char *text)
{
    while (text != 0 && *text != '\0') {
        c26_uart_putc(*text++);
    }
}

void c26_put_uint(uint64_t value)
{
    char digits[21];
    size_t used = 0;
    if (value == 0) {
        c26_uart_putc('0');
        return;
    }
    while (value != 0 && used < sizeof(digits)) {
        digits[used++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (used != 0) {
        c26_uart_putc(digits[--used]);
    }
}

void c26_put_hex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    c26_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        c26_uart_putc(hex[(value >> shift) & 0xf]);
    }
}
