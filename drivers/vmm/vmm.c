#include <vmm.h>

#include <debug.h>
#include <heap.h>
#include <limine.h>
#include <panic.h>
#include <string.h>

// Page Map Level 4 Table (PML4T)
static uint64_t *pml4 = NULL;

// Helper function to get the HHDM offset
static uint64_t get_hhdm_offset()
{
    if (hhdm_request.response == NULL) {
        // This should not happen if the bootloader is Limine compliant
        // and the request is in the right section.
        panic("VMM: HHDM request not honored by bootloader.");
    }
    return hhdm_request.response->offset;
}

// Helper to convert physical address to virtual using HHDM
void *phys_to_virt(void *phys_addr)
{
    return (void *)((uintptr_t)phys_addr + get_hhdm_offset());
}

// Helper to convert virtual address to physical using HHDM
void *virt_to_phys(void *virt_addr)
{
    return (void *)((uintptr_t)virt_addr - get_hhdm_offset());
}

/**
 * @brief Gets the next level page table from an entry.
 * If the entry is not present and create is true, a new page table is
 * allocated.
 *
 * @param entry Pointer to the page table entry.
 * @param create If true, create a new page table if not present.
 * @return Pointer to the next level page table, or NULL on failure.
 */
static uint64_t *get_next_table(uint64_t *entry, bool create)
{
    if (*entry & VMM_PRESENT) {
        return phys_to_virt((void *)(*entry & ~0xFFF));
    }

    if (!create) {
        return NULL;
    }

    // Allocate a new page-aligned table
    void *new_table = malloc(PAGE_SIZE);
    if (!new_table) {
        log_err("VMM: Failed to allocate page table.");
        return NULL;
    }
    memset(new_table, 0, PAGE_SIZE);

    // The address from malloc is virtual, convert to physical for the entry
    *entry =
        (uint64_t)virt_to_phys(new_table) | VMM_PRESENT | VMM_WRITE | VMM_USER;

    return new_table;
}

/**
 * @brief Maps a single page.
 *
 * @param pml4_table The base of the PML4 table.
 * @param virt_addr The virtual address to map.
 * @param phys_addr The physical address to map to.
 * @param flags The flags for the page table entry.
 * @return true on success, false on failure.
 */
static bool map_page(uint64_t *pml4_table, void *virt_addr, void *phys_addr,
                     uint32_t flags)
{
    uintptr_t virt = (uintptr_t)virt_addr;

    size_t pml4_index = (virt >> 39) & 0x1FF;
    size_t pdpt_index = (virt >> 30) & 0x1FF;
    size_t pd_index = (virt >> 21) & 0x1FF;
    size_t pt_index = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_next_table(&pml4_table[pml4_index], true);
    if (!pdpt) {
        return false;
    }

    uint64_t *pd = get_next_table(&pdpt[pdpt_index], true);
    if (!pd) {
        return false;
    }

    uint64_t *pt = get_next_table(&pd[pd_index], true);
    if (!pt) {
        return false;
    }

    pt[pt_index] = (uintptr_t)phys_addr | flags;
    return true;
}

void vmm_init()
{
    // Get the current PML4 table from CR3
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4 = phys_to_virt((void *)cr3);

    log_info("VMM: Initialized with PML4 at phys 0x%x, virt 0x%x", cr3, pml4);
}

void *mmap_physical(void *phys_addr, size_t size, uint32_t flags)
{
    if (!pml4) {
        log_err("VMM: mmap_physical called before vmm_init().");
        return NULL;
    }

    // For simplicity, we'll return the physical address as the virtual address.
    // This is not ideal for a real-world scenario but works for MMIO where
    // addresses are high and unlikely to conflict with kernel/user space.
    // A more robust implementation would find a free virtual address range.
    void *virt_addr = phys_addr;

    uintptr_t phys_start = (uintptr_t)phys_addr;
    uintptr_t virt_start = (uintptr_t)virt_addr;

    // Align to page boundaries
    uintptr_t phys_aligned = phys_start & ~(PAGE_SIZE - 1);
    uintptr_t virt_aligned = virt_start & ~(PAGE_SIZE - 1);
    size_t num_pages =
        ((phys_start + size - phys_aligned) + PAGE_SIZE - 1) / PAGE_SIZE;

    log_verbose("VMM: Mapping %d pages from phys 0x%x to virt 0x%x", num_pages,
                phys_aligned, virt_aligned);

    for (size_t i = 0; i < num_pages; i++) {
        void *current_phys = (void *)(phys_aligned + i * PAGE_SIZE);
        void *current_virt = (void *)(virt_aligned + i * PAGE_SIZE);

        if (!map_page(pml4, current_virt, current_phys, flags)) {
            log_err("VMM: Failed to map page at virt 0x%x", current_virt);
            // TODO: Unmap already mapped pages on failure
            return NULL;
        }
    }

    // Invalidate TLB for the changed virtual addresses
    for (size_t i = 0; i < num_pages; i++) {
        asm volatile(
            "invlpg (%0)" ::"r"((void *)(virt_aligned + i * PAGE_SIZE)));
    }

    return virt_addr;
}
