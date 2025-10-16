#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <string.h>
#include <tty.h>
#include <vga.h>
#include <math.h>

static size_t term_cursor_y;
static size_t term_cursor_x;
static uint8_t term_color;
static uint16_t* term_buffer = (uint16_t*)VGA_MEMORY;

void term_init(void) {
	term_cursor_y = 0;
	term_cursor_x = 0;
	term_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	term_clear();
}
void term_clear(void) {	
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			term_buffer[index] = vga_entry(' ', term_color);
		}
	}
}

void term_set_cursor_pos(size_t x, size_t y) {
	term_cursor_x = x;
	term_cursor_y = y;
}

void term_set_x(size_t c) {
	term_cursor_x = c;
}

void term_set_y(size_t c) {
	term_cursor_y = c;
}

void term_set_color(enum vga_color fg, enum vga_color bg) {
	term_color = vga_entry_color(fg, bg);
}

void term_set_color_entry(uint8_t color) {
	term_color = color;
}

void term_reset_color(enum vga_color fg, enum vga_color bg) {
	term_color = vga_entry_color(fg, bg);
}

uint8_t term_get_color_entry(void) {
	return term_color;
}

void term_putentryat(char c, uint8_t color, size_t x, size_t y) {
	const size_t index = y * VGA_WIDTH + x;
	term_buffer[index] = vga_entry(c, color);
}

void term_putchar(char c) {
	term_putentryat(c, term_color, term_cursor_x, term_cursor_y);
	if (++term_cursor_x == VGA_WIDTH) {
		term_cursor_x = 0;
		if (++term_cursor_y == VGA_HEIGHT)
			term_cursor_y = 0;
	}
}

void term_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++) {
		switch (data[i]) {
			case '\n':
				term_newline();
			break;

			default:
				putchar(data[i]);
			break;
		}
	}
}

void term_writestring(const char* data) {
	term_write(data, strlen(data));
}

void term_writestringln(const char* data) {
	term_write(data, strlen(data));
	term_newline();
}

void term_newline(void) {
	term_cursor_y++;
	term_cursor_x = 0;
}

void term_print_centered(const char* text) {
	uint8_t screen_midpoint = round_to_even(VGA_WIDTH / 2, false);
	uint8_t text_midpoint = round_to_even(strlen(text) / 2, false);
	
	term_cursor_x = screen_midpoint - text_midpoint;
	term_writestringln(text);
	term_newline();
}