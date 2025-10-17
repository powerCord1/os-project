#include <tty.h>
#include <stdio.h>
#include <cpu.h>

void panic(const char* reason) {
    term_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    printf("KERNEL PANIC: %s", reason);
    halt();
}