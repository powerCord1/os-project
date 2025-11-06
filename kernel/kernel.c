#include <app.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <cpuid.h>
#include <debug.h>
#include <gdt.h>
#include <graphics.h>
#include <heap.h>
#include <interrupts.h>
#include <keyboard.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <security.h>
#include <serial.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>

void main_menu(void);

void main(void)
{
    serial_init();

    log_verbose("Kernel: Initializing GDT...");
    gdt_init();
    log_verbose("Kernel: Initializing IDT...");
    idt_init();
    log_verbose("Kernel: Initializing Heap...");
    heap_init();
    log_verbose("Kernel: Initializing PIT...");
    pit_init(1000);
    log_verbose("Kernel: Initializing TTY...");
    term_init();

    log_verbose("Kernel: Entering main menu...");
    main_menu();

    while (1) {
        idle();
    }
}

void main_menu(void)
{
    app_t apps[] = {{"Typewriter", &typewriter_main},
                    {"Key notes", &key_notes_main},
                    {"Heap test", &heap_test_main},
                    {"Shell", &shell_main},
                    {"Stack Smash Test", &ssp_test_main}};
    size_t app_count = sizeof(apps) / sizeof(app_t);

    while (1) {
        term_clear();
        gfx_draw_title("MAIN MENU");
        printf("Select an app to launch:\n");
        for (size_t i = 0; i < app_count; i++) {
            printf("%d. %s\n", i + 1, apps[i].name);
        }

        char choice = kbd_get_last_char(true);
        size_t choice_index = choice - '1';
        if (choice_index > app_count - 1) {
            continue;
        } else {
            term_clear();
            gfx_draw_title(apps[choice_index].name);
            apps[choice_index].entry();
        }
    }
}