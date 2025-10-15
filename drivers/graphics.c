#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <graphics.h>
#include <tty.h>
#include <vga.h>
#include <math.h>

void gfx_filled_line(void) {
	uint8_t prev_color = term_get_color_entry();
    term_set_x(0);
	term_set_color(VGA_COLOR_WHITE, VGA_COLOR_WHITE);
	for (size_t i = 0; i < VGA_WIDTH; i++) {
		term_putchar(' ');
	}
	term_set_color_entry(prev_color);
}

void gfx_draw_title(const char* text) {
	uint8_t orig_color = term_get_color_entry();
    uint8_t height = 3;

    // draw white block
    term_set_cursor_pos(0, 0);
    for (size_t i = 0; i < height * VGA_WIDTH; i++) {
        term_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_WHITE);
        term_putchar(' ');
    }

    // draw text in middle of block
    term_set_cursor_pos(0, floordiv2(height));
    term_print_centered("middle");

    // set cursor after title
    term_set_cursor_pos(0, height + 1);

    term_set_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
	term_set_color_entry(orig_color);
}