#include <cpu.h>
#include <debug.h>
#include <interrupts.h>
#include <keyboard.h>
#include <power.h>
#include <stdio.h>
#include <tty.h>

__attribute__((noreturn)) void panic(const char *reason)
{
    log_err("PANIC: %s", reason);

#if !DEBUG
    term_clear();
#endif
    term_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    printf("KERNEL PANIC: %s\n", reason);
#if HALT_ON_PANIC
    halt_catch_fire();
#else
    printf("Press any key to reboot");
    kbd_poll_key();
    reboot();
#endif
}