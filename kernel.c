#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cpu.h>
#include <cpuid.h>
#include <debug.h>
#include <graphics.h>
#include <interrupts.h>
#include <keyboard.h>
#include <panic.h>
#include <power.h>
#include <stdio.h>
#include <tty.h>

void main(void)
{
    idt_init();
    term_init();
    gfx_draw_title("TITLE");
    log_verbose("verbose");
    log_info("info");
    log_warn("warning");
    log_err("error");
    while (1) {
        halt();
    }
}