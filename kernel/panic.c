#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <interrupts.h>
#include <keyboard.h>
#include <panic.h>
#include <power.h>
#include <stdio.h>

__attribute__((noreturn)) void panic(const char *reason)
{
    log_err("PANIC: %s", reason);

#if CLEAR_ON_PANIC
    fb_clear();
#else
    fb_newline();
#endif
    fb_set_color(0xFF0000, 0);
    printf("KERNEL PANIC: %s\n", reason);
#if HALT_ON_PANIC
    halt_cf();
#else
    printf("Press any key to reboot");
    kbd_poll_key();
    sys_reset();
#endif
}