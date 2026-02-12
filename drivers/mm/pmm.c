#include <debug.h>
#include <limine.h>
#include <panic.h>
#include <pmm.h>
#include <stdbool.h>
#include <string.h>

#define PAGE_SIZE 4096

static uint8_t *bitmap;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t last_index = 0;

static void bitmap_set(uint64_t index)
{
    bitmap[index / 8] |= (1 << (index % 8));
}

static void bitmap_clear(uint64_t index)
{
    bitmap[index / 8] &= ~(1 << (index % 8));
}

static bool bitmap_test(uint64_t index)
{
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

void pmm_init()
{
    struct limine_memmap_response *memmap = memmap_request.response;
    struct limine_hhdm_response *hhdm = hhdm_request.response;

    if (memmap == NULL) {
        panic("PMM: Limine memmap response is NULL");
    }
    if (hhdm == NULL) {
        panic("PMM: Limine HHDM response is NULL");
    }

    uint64_t highest_address = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t top = entry->base + entry->length;
        if (top > highest_address) {
            highest_address = top;
        }
    }

    total_pages = highest_address / PAGE_SIZE;
    uint64_t bitmap_size = (total_pages + 7) / 8;

    // Find a place for the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap = (uint8_t *)(entry->base + hhdm->offset);
            // Mark all as used initially
            memset(bitmap, 0xFF, bitmap_size);
            break;
        }
    }

    if (bitmap == NULL) {
        panic("PMM: Could not find a suitable location for the bitmap");
    }

    // Mark usable regions as free
    free_pages = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
                bitmap_clear((entry->base + j) / PAGE_SIZE);
                free_pages++;
            }
        }
    }

    // Mark bitmap pages as used
    uint64_t bitmap_phys = (uint64_t)bitmap - hhdm->offset;
    for (uint64_t i = 0; i < (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE; i++) {
        bitmap_set(bitmap_phys / PAGE_SIZE + i);
        free_pages--;
    }

    // Also mark first page as used to avoid returning 0 as a valid address
    if (!bitmap_test(0)) {
        bitmap_set(0);
        free_pages--;
    }

    log_info("PMM: Initialized with %d MB total, %d free pages",
             highest_address / 1024 / 1024, free_pages);
}

void *pmm_alloc_page()
{
    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t index = (last_index + i) % total_pages;
        if (!bitmap_test(index)) {
            bitmap_set(index);
            last_index = index;
            free_pages--;
            return (void *)(index * PAGE_SIZE);
        }
    }

    log_warn("PMM: Out of memory!");
    return NULL;
}

void pmm_free_page(void *page_addr)
{
    uint64_t index = (uint64_t)page_addr / PAGE_SIZE;
    if (bitmap_test(index)) {
        bitmap_clear(index);
        free_pages++;
    }
}
