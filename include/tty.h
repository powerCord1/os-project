#include <stddef.h>

#include <vga.h>

void term_init(void);
void term_clear(void);
void term_set_cursor_pos(size_t x, size_t y);
void term_set_x(size_t c);
void term_set_y(size_t c);
void term_putchar(char c);
void term_write(const char* data, size_t size);
void term_writestring(const char* data);
void term_set_color(enum vga_color fg, enum vga_color bg);
void term_set_color_entry(uint8_t color);
uint8_t term_get_color_entry(void);
void term_newline(void);
void term_print_centered(const char* text);