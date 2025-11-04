#include <cpu.h>
#include <stdio.h>
#include <tty.h>

__attribute__((noreturn)) void panic(const char *reason)
{
    term_clear();
    term_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    printf("KERNEL PANIC: %s", reason);
    halt_catch_fire();
}