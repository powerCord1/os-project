#include <app.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <graphics.h>
#include <init.h>
#include <interrupts.h>
#include <keyboard.h>
#include <multiboot2.h>
#include <pit.h>
#include <power.h>
#include <stdio.h>

void main_menu();

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
    main_menu();

    while (1) {
        idle();
    }
}

void main_menu()
{
    app_t apps[] = {
        {"Typewriter", &typewriter_main},     {"Key notes", &key_notes_main},
        {"Heap test", &heap_test_main},       {"Shell", &shell_main},
        {"Stack Smash Test", &ssp_test_main}, {"Shutdown", &shutdown}};
    size_t app_count = sizeof(apps) / sizeof(app_t);

    while (1) {
        log_verbose("Refreshing main menu");
        fb_clear();
        fb_draw_title("MAIN MENU");
        printf("Select an app to launch:\n");
        for (size_t i = 0; i < app_count; i++) {
            printf("%d. %s\n", i + 1, apps[i].name);
        }

        char choice = kbd_get_key(true).key;
        size_t choice_index = choice - '1';
        if (choice_index > app_count - 1) {
            continue;
        } else {
            fb_clear();
            fb_draw_title(apps[choice_index].name);
            log_verbose("Launching app: %s", apps[choice_index].name);
            apps[choice_index].entry();
        }
    }
}