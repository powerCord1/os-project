#include <io.h>
#include <interrupts.h>
#include <debug.h>
#include <mouse.h>
#include <keyboard.h>
#include <stddef.h>

uint64_t mouse_handler(uint64_t rsp, void *ctx) {
    (void)ctx;
    // Drain the PS/2 output buffer
    // We use a single read per interrupt to avoid potential infinite loops on real hardware
    if (inb(KBD_STATUS_PORT) & 1) {
        // Read the data byte to acknowledge the interrupt and clear the buffer
        inb(KBD_DATA_PORT);
    }
    return rsp;
}

void mouse_init() {
    log_info("Initialising mouse dummy handler");
    irq_install_handler(IRQ_TYPE_MOUSE, mouse_handler, NULL);
}
