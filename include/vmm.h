#pragma once

#include <stddef.h>
#include <stdint.h>

// Common page size for x86 architectures
#define PAGE_SIZE 4096

// --- Page Table / Page Directory Entry Flags ---
// These correspond to the bits in a page table entry.

// Set if the page is present in memory
#define VMM_PRESENT (1 << 0)
// Set if the page is writable
#define VMM_WRITE (1 << 1)
// Set if the page is accessible from user mode
#define VMM_USER (1 << 2)

/**
 * @brief Initializes the virtual memory manager.
 *
 * This function should set up the initial page directory and page tables
 * to enable paging.
 */
void vmm_init();

/**
 * @brief Maps a physical memory region to a virtual address space.
 *
 * @param phys_addr The starting physical address to map.
 * @param size The size of the memory region to map.
 * @param flags Page table flags (e.g., VMM_PRESENT, VMM_WRITE).
 * @return The virtual address corresponding to the start of the mapped region.
 */
void *mmap_physical(void *virt_addr, void *phys_addr, size_t size,
                    uint32_t flags);

// Helper to convert physical address to virtual using HHDM
void *phys_to_virt(void *phys_addr);

// Helper to convert virtual address to physical using HHDM

void *virt_to_phys(void *virt_addr);



// Helper to convert virtual address to physical by walking the page tables

void *vmm_get_phys(void *virt_addr);