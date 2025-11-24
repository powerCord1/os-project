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

    enable_interrupts();

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

void main_menu()
{
    menu_t apps[] = {{"Typewriter", &typewriter_main},
                     {"Key notes", &key_notes_main},
                     {"Heap test", &heap_test_main},
                     {"Shell", &shell_main},
                     {"Stack Smash Test", &ssp_test_main},
                     {"Element drawing test", &element_test},
                     {"PIT test", &pit_test_main},
                     {"Memory test", &memory_test_main},
                     {"Sin Wave Test", &sin_test_main}};
    create_menu("Main menu", "Choose an app to launch", apps,
                sizeof(apps) / sizeof(menu_t));
}

void power_menu()
{
    log_info("Entering power menu");
    menu_t options[] = {{"Reboot", &reboot}, {"Shutdown", &shutdown}};
    create_menu("Power menu", "Select an option:", options,
                sizeof(options) / sizeof(menu_t));
    fb_clear();
}