#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Allocates a single physical page of memory.
 *
 * @return A pointer to the physical address of the allocated page, or NULL if
 * no pages are available.
 */
void *pmm_alloc_page();

/**
 * @brief Frees a previously allocated physical page.
 *
 * @param page_addr The physical address of the page to free.
 */
void pmm_free_page(void *page_addr);

/**
 * @brief Initializes the physical memory manager using the memory map.
 */
void pmm_init();