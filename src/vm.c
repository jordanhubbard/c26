#include "c26.h"
#include "c26_user.h"

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)
#define PAGE_SIZE 4096UL
#define TABLE_POOL_PAGES 16U
#define USER_REGION_MAX 8U

typedef struct {
    uint64_t base;
    uint64_t size;
    int writable;
} vm_region_t;

static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint64_t table_pool[TABLE_POOL_PAGES][512] __attribute__((aligned(4096)));
static unsigned int tables_used;
static vm_region_t regions[USER_REGION_MAX];
static unsigned int region_count;

static uint64_t *alloc_table(void)
{
    if (tables_used == TABLE_POOL_PAGES) {
        return 0;
    }
    uint64_t *table = table_pool[tables_used++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

void c26_vm_reset(void)
{
    memset(root_table, 0, sizeof(root_table));
    tables_used = 0;
    region_count = 0;
}

static int map_page(uint64_t va, uint64_t pa, uint64_t flags)
{
    uint64_t *table = root_table;
    for (int level = 2; level > 0; level--) {
        unsigned int index = (va >> (12 + 9 * level)) & 0x1ff;
        if ((table[index] & PTE_V) == 0) {
            uint64_t *next = alloc_table();
            if (next == 0) {
                return 0;
            }
            table[index] = ((uint64_t)(uintptr_t)next >> 12) << 10 | PTE_V;
        }
        table = (uint64_t *)(uintptr_t)((table[index] >> 10) << 12);
    }
    table[(va >> 12) & 0x1ff] = (pa >> 12) << 10 | flags | PTE_A | PTE_D | PTE_V;
    return 1;
}

int c26_vm_map_user(uint64_t base, uint64_t size, int writable, int executable)
{
    if ((base & (PAGE_SIZE - 1)) != 0 || size == 0 ||
        region_count == USER_REGION_MAX) {
        return 0;
    }
    uint64_t flags = PTE_U | PTE_R;
    if (writable) flags |= PTE_W;
    if (executable) flags |= PTE_X;
    uint64_t end = (base + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t va = base; va < end; va += PAGE_SIZE) {
        if (!map_page(va, va, flags)) {
            return 0;
        }
    }
    regions[region_count++] = (vm_region_t){base, end - base, writable};
    return 1;
}

void c26_vm_activate(void)
{
    uint64_t satp = (8UL << 60) | ((uint64_t)(uintptr_t)root_table >> 12);
    __asm__ volatile("csrw satp, %0" ::"r"(satp));
    __asm__ volatile("sfence.vma zero, zero");
}

int c26_vm_user_range(uint64_t base, uint64_t size, int write)
{
    if (size == 0) {
        return 1;
    }
    for (unsigned int i = 0; i < region_count; i++) {
        if (base >= regions[i].base && size <= regions[i].size &&
            base - regions[i].base <= regions[i].size - size) {
            return !write || regions[i].writable;
        }
    }
    return 0;
}
