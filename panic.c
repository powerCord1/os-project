#include <tty.h>
#include <stdio.h>
#include <x86/cpu.h>

void panic(const char* reason) {
    term_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    printf("KERNEL PANIC: %s", reason);
    halt();
    printf("shouldn't ever run");
}