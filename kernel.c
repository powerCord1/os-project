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
	gfx_draw_title("TITLE");
	term_writestringln("writestring");
	term_print_centered("centered text");
	log_verbose("verbose");
	log_info("info");
	log_warning("warning");
	log_error("error");

	term_newline();
	for (int i = 0; i < 256; i++) {
		putchar(i);
	}
}