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
    /* The interrupt handler is the only RBR reader; consuming here raced
       it and duplicated bytes. Draining the ring re-enables RX interrupts
       in case the handler masked them under backpressure. */
    c26_uart_enable_interrupt();
    if (receive_tail != receive_head) {
        uint8_t value = receive_buffer[receive_tail & 0xffU];
        receive_tail++;
        return value;
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
        uint16_t next = (uint16_t)(receive_head + 1);
        if ((uint16_t)(next - receive_tail) > sizeof(receive_buffer)) {
            /* Ring full: mask the RX interrupt and leave the byte in the
               device so QEMU applies backpressure instead of us dropping
               data. The reader re-enables the interrupt as it drains. */
            uart0[UART_IER] = 0;
            return;
        }
        receive_buffer[receive_head & 0xffU] = uart0[UART_RBR];
        receive_head = next;
    }
}

