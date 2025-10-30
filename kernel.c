#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <cpuid.h>
#include <debug.h>
#include <gdt.h>
#include <graphics.h>
#include <interrupts.h>
#include <keyboard.h>
#include <panic.h>
#include <power.h>
#include <serial.h>
#include <stdio.h>
#include <string.h>
#include <tty.h>

void main(void)
{
    term_init();
    gdt_init();
    idt_init();
    init_serial();
    gfx_draw_title("TYPEWRITER");
    while (1) {
        halt();
        // term_scroll(1);
    }
}