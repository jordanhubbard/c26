#include "c26.h"

#define CLINT_BASE 0x02000000UL
#define CLINT_MTIMECMP (CLINT_BASE + 0x4000UL)
#define CLINT_MTIME (CLINT_BASE + 0xbff8UL)
#define PLIC_BASE 0x0c000000UL
#define PLIC_ENABLE (PLIC_BASE + 0x2000UL)
#define PLIC_THRESHOLD (PLIC_BASE + 0x200000UL)
#define PLIC_CLAIM (PLIC_BASE + 0x200004UL)
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060UL
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064UL
#define TIMER_INTERVAL 100000UL

#define MCAUSE_INTERRUPT (1ULL << 63)
#define MCAUSE_MACHINE_TIMER 7U
#define MCAUSE_MACHINE_EXTERNAL 11U
#define MIE_MTIE (1UL << 7)
#define MIE_MEIE (1UL << 11)
#define MSTATUS_MIE (1UL << 3)

extern void c26_trap_entry(void);

static volatile uint64_t timer_ticks;
static volatile uint64_t external_interrupts;
static uint64_t next_timer;

static uint64_t read_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static void handle_external_interrupt(void)
{
    volatile uint32_t *claim = (volatile uint32_t *)PLIC_CLAIM;
    for (;;) {
        uint32_t source = *claim;
        if (source == 0) {
            return;
        }
        external_interrupts++;
        if (source >= 1 && source <= 8) {
            uintptr_t base = 0x10000000UL + (uintptr_t)source * 0x1000UL;
            volatile uint32_t *status =
                (volatile uint32_t *)(base + VIRTIO_MMIO_INTERRUPT_STATUS);
            volatile uint32_t *ack =
                (volatile uint32_t *)(base + VIRTIO_MMIO_INTERRUPT_ACK);
            uint32_t pending = *status;
            if (pending != 0) {
                *ack = pending;
            }
        } else if (source == 10) {
            c26_uart_handle_interrupt();
        }
        *claim = source;
    }
}

void c26_trap_handler(void)
{
    uint64_t cause = read_mcause();
    if ((cause & MCAUSE_INTERRUPT) == 0) {
        c26_puts("FATAL TRAP: mcause=");
        c26_put_hex(cause);
        c26_uart_putc('\n');
        for (;;) {
            __asm__ volatile("wfi");
        }
    }
    cause &= ~MCAUSE_INTERRUPT;
    if (cause == MCAUSE_MACHINE_TIMER) {
        timer_ticks++;
        uint64_t now = *(volatile uint64_t *)CLINT_MTIME;
        do {
            next_timer += TIMER_INTERVAL;
        } while (next_timer <= now);
        *(volatile uint64_t *)CLINT_MTIMECMP = next_timer;
    } else if (cause == MCAUSE_MACHINE_EXTERNAL) {
        handle_external_interrupt();
    }
}

void c26_interrupts_init(void)
{
    __asm__ volatile("csrw mtvec, %0" :: "r"(c26_trap_entry));

    for (uint32_t source = 1; source <= 10; source++) {
        *(volatile uint32_t *)(PLIC_BASE + source * 4U) = 1;
    }
    *(volatile uint32_t *)PLIC_ENABLE = 0x000007feU;
    *(volatile uint32_t *)PLIC_THRESHOLD = 0;

    c26_uart_enable_interrupt();
    next_timer = *(volatile uint64_t *)CLINT_MTIME + TIMER_INTERVAL;
    *(volatile uint64_t *)CLINT_MTIMECMP = next_timer;

    uintptr_t mie = MIE_MTIE | MIE_MEIE;
    __asm__ volatile("csrw mie, %0" :: "r"(mie));
    __asm__ volatile("csrs mstatus, %0" :: "r"((uintptr_t)MSTATUS_MIE));
    c26_puts("INTERRUPTS: CLINT timer + PLIC online, idle uses WFI\n");
}

uint64_t c26_interrupt_ticks(void)
{
    return timer_ticks;
}

uint64_t c26_interrupt_external_count(void)
{
    return external_interrupts;
}

void c26_idle(void)
{
    __asm__ volatile("wfi");
}
