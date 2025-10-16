#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tty.h>
#include <string.h>
#include <graphics.h>
#include <panic.h>
#include <debug.h>
#include <power.h>

void main(void) {
	term_init();
	gfx_draw_title("test");
	term_writestringln("writestring");
	term_print_centered("centered text");
	log_info("log test");
	panic("panic test");
}