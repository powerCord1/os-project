#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <graphics.h>
#include <panic.h>
#include <power.h>
#include <string.h>
#include <tty.h>

void main(void)
{
    term_init();
    gfx_draw_title("test");
    term_writestring("writestring");
    term_print_centered("centered text");
    log_info("log test");
    panic("panic test");
}