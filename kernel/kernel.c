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
#include <menus.h>
#include <multiboot2.h>
#include <panic.h>
#include <pit.h>
#include <power.h>
#include <scheduler.h>
#include <stdio.h>

void main()
{
    sys_init();
    log_info("Kernel initialized");

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