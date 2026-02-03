#include <stddef.h>

#include <limine_defs.h>

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// LIMINE_BASE_REVISION(1);

// Request a 64-bit kernel entry point.
volatile struct limine_entry_point_request entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    // We'll let the linker fill in the entry point address.
    .entry = NULL};

// Request a framebuffer.
// volatile struct limine_framebuffer_request framebuffer_request = {
//     .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = NULL};

// Request a memory map.
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = NULL};