#include "c26.h"
#include "c26_user.h"

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

/* Per-hart timer state: each hart has its own CLINT mtimecmp compare register
   at CLINT_MTIMECMP + 8*hartid and its own tick counter. */
#define CLINT_MTIMECMP_HART(h) (CLINT_BASE + 0x4000UL + 8UL * (uint64_t)(h))
static volatile uint64_t timer_ticks[C26_NHART];
static volatile uint64_t external_interrupts;
static uint64_t next_timer[C26_NHART];

static inline int hartid(void)
{
    uint64_t id;
    __asm__ volatile("csrr %0, mhartid" : "=r"(id));
    return (int)id;
}

static uint64_t read_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

void c26_timer_interrupt(void)
{
    int h = hartid();
    timer_ticks[h]++;
    uint64_t now = *(volatile uint64_t *)CLINT_MTIME;
    do {
        next_timer[h] += TIMER_INTERVAL;
    } while (next_timer[h] <= now);
    *(volatile uint64_t *)CLINT_MTIMECMP_HART(h) = next_timer[h];
}

void c26_external_interrupt(void)
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
        /* Per-hart timer state only — no shared state, so no lock needed. */
        c26_timer_interrupt();
    } else if (cause == MCAUSE_MACHINE_EXTERNAL) {
        /* Device state is shared with app syscalls on other harts. */
        c26_kernel_lock();
        c26_external_interrupt();
        c26_kernel_unlock();
    }
}

void c26_interrupts_init(void)
{
    __asm__ volatile("csrw mtvec, %0" :: "r"(c26_trap_entry));
    __asm__ volatile("csrw mscratch, zero");

    /* PMP: without a matching entry, U-mode accesses fault regardless of
       page tables. Grant everything here; Sv39 is the real isolation. */
    __asm__ volatile("csrw pmpaddr0, %0" :: "r"(~0UL >> 10));
    __asm__ volatile("csrw pmpcfg0, %0" :: "r"(0x1fUL));

    for (uint32_t source = 1; source <= 10; source++) {
        *(volatile uint32_t *)(PLIC_BASE + source * 4U) = 1;
    }
    *(volatile uint32_t *)PLIC_ENABLE = 0x000007feU;
    *(volatile uint32_t *)PLIC_THRESHOLD = 0;

    c26_uart_enable_interrupt();
    next_timer[0] = *(volatile uint64_t *)CLINT_MTIME + TIMER_INTERVAL;
    *(volatile uint64_t *)CLINT_MTIMECMP_HART(0) = next_timer[0];

    uintptr_t mie = MIE_MTIE | MIE_MEIE;
    __asm__ volatile("csrw mie, %0" :: "r"(mie));
    __asm__ volatile("csrs mstatus, %0" :: "r"((uintptr_t)MSTATUS_MIE));
    c26_puts("INTERRUPTS: CLINT timer + PLIC online, idle uses WFI\n");
}

/* Per-hart bring-up for a secondary hart: its own trap vector, PMP, and CLINT
   timer. Secondaries take only their timer (for preemption); device (external)
   interrupts stay routed to hart 0. */
void c26_hart_init(void)
{
    int h = hartid();
    __asm__ volatile("csrw mtvec, %0" :: "r"(c26_trap_entry));
    __asm__ volatile("csrw mscratch, zero");
    __asm__ volatile("csrw pmpaddr0, %0" :: "r"(~0UL >> 10));
    __asm__ volatile("csrw pmpcfg0, %0" :: "r"(0x1fUL));
    next_timer[h] = *(volatile uint64_t *)CLINT_MTIME + TIMER_INTERVAL;
    *(volatile uint64_t *)CLINT_MTIMECMP_HART(h) = next_timer[h];
    __asm__ volatile("csrw mie, %0" :: "r"((uintptr_t)MIE_MTIE));
    __asm__ volatile("csrs mstatus, %0" :: "r"((uintptr_t)MSTATUS_MIE));
}

uint64_t c26_interrupt_ticks(void)
{
    /* Hart 0's tick is the machine's monotonic 100 Hz clock (animations, RTC-
       independent timing all run on hart 0), independent of how many harts run. */
    return timer_ticks[0];
}

uint64_t c26_hart_ticks(int h)
{
    return (h >= 0 && h < C26_NHART) ? timer_ticks[h] : 0;
}

uint64_t c26_interrupt_external_count(void)
{
    return external_interrupts;
}

void c26_idle(void)
{
    __asm__ volatile("wfi");
}

uint64_t c26_rtc_seconds(void)
{
    /* Goldfish RTC on QEMU virt: 64-bit nanoseconds since the Unix epoch,
       read low word first to latch. */
    volatile uint32_t *rtc = (volatile uint32_t *)0x101000UL;
    uint64_t low = rtc[0];
    uint64_t high = rtc[1];
    uint64_t nanos = (high << 32) | low;
    return nanos / 1000000000ULL;
}

void c26_poweroff(void)
{
    /* SiFive test finisher on QEMU virt: a real emulated device whose
       0x5555 command powers the machine off. */
    *(volatile uint32_t *)0x100000UL = 0x5555;
    for (;;) {
        __asm__ volatile("wfi");
    }
}
