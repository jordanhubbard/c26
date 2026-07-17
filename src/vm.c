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

static uint64_t *alloc_table(c26_vm_space_t *space)
{
    if (space->tables_used == C26_VM_POOL_PAGES) {
        return 0;
    }
    uint64_t *table = space->pool[space->tables_used++];
    memset(table, 0, PAGE_SIZE);
    return table;
}

void c26_vm_init(c26_vm_space_t *space)
{
    memset(space->root, 0, sizeof(space->root));
    space->tables_used = 0;
    space->region_count = 0;
}

static int map_page(c26_vm_space_t *space, uint64_t va, uint64_t pa,
                    uint64_t flags)
{
    uint64_t *table = space->root;
    for (int level = 2; level > 0; level--) {
        unsigned int index = (va >> (12 + 9 * level)) & 0x1ff;
        if ((table[index] & PTE_V) == 0) {
            uint64_t *next = alloc_table(space);
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

int c26_vm_map(c26_vm_space_t *space, uint64_t va, uint64_t pa, uint64_t size,
               int writable, int executable)
{
    if ((va & (PAGE_SIZE - 1)) != 0 || (pa & (PAGE_SIZE - 1)) != 0 ||
        size == 0 || space->region_count == C26_VM_REGION_MAX) {
        return 0;
    }
    uint64_t flags = PTE_U | PTE_R;
    if (writable) flags |= PTE_W;
    if (executable) flags |= PTE_X;
    uint64_t length = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t offset = 0; offset < length; offset += PAGE_SIZE) {
        if (!map_page(space, va + offset, pa + offset, flags)) {
            return 0;
        }
    }
    c26_vm_region_t *region = &space->regions[space->region_count++];
    region->va = va;
    region->pa = pa;
    region->size = length;
    region->writable = writable;
    return 1;
}

void c26_vm_activate(c26_vm_space_t *space)
{
    uint64_t satp = (8UL << 60) | ((uint64_t)(uintptr_t)space->root >> 12);
    __asm__ volatile("csrw satp, %0" ::"r"(satp));
    __asm__ volatile("sfence.vma zero, zero");
}

/* Translates a user range to a kernel-usable physical pointer, or 0 when
 * any byte falls outside the process's mapped regions (or write access is
 * requested on a read-only region). */
uintptr_t c26_vm_translate(const c26_vm_space_t *space, uint64_t va,
                           uint64_t size, int write)
{
    if (size == 0) {
        size = 1;
    }
    for (unsigned int i = 0; i < space->region_count; i++) {
        const c26_vm_region_t *region = &space->regions[i];
        if (va >= region->va && size <= region->size &&
            va - region->va <= region->size - size) {
            if (write && !region->writable) {
                return 0;
            }
            return (uintptr_t)(region->pa + (va - region->va));
        }
    }
    return 0;
}
