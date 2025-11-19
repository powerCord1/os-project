#include <stdbool.h>
#include <stddef.h>

#include <cpu.h>
#include <debug.h>
#include <limine.h>
#include <limine_defs.h>
#include <panic.h>

__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(4);

__attribute__((
    used,
    section(".limine_requests"))) volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((used,
               section(".limine_requests_start"))) static volatile uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void limine_init()
{
    log_verbose("Checking if Limine revision is supported");
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        panic("Limine base revision not supported.");
    }

    log_verbose("Checking for framebuffers");
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        panic("No framebuffers found.");
    } else {
        log_verbose("Found %d framebuffers",
                    framebuffer_request.response->framebuffer_count);
    }
}