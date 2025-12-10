#include <app.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <init.h>
#include <interrupts.h>
#include <keyboard.h>
#include <menu.h>
#include <multiboot2.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <stdio.h>

void main_menu();
void power_menu();

void main(unsigned long magic, unsigned long addr)
{
    struct multiboot_tag *tag;

    sys_init();

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        log_info("Booted via multiboot2.");
        for (tag = (struct multiboot_tag *)(addr + 8);
             tag->type != MULTIBOOT_TAG_TYPE_END;
             tag = (struct multiboot_tag *)((uint8_t *)tag +
                                            ((tag->size + 7) & ~7))) {
            log_verbose("Tag 0x%x, Size 0x%x", tag->type, tag->size);
        }
    }

    log_verbose("Init done, entering main menu...");
    while (1) {
        main_menu();
        power_menu();
    }
}

__attribute__((noreturn)) void main_exit()
{
    panic("init exited");
}