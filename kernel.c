#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tty.h>
#include <stdlib.h>
#include <graphics.h>

void main(void) {
	term_init();
	gfx_draw_title("test");
	term_writestring("24k gold labubu dubai chocolate 67 matcha latte w/ moonbeam ice cream\nline2");
	term_print_centered("qwertyuiop");
}