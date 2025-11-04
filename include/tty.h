#include <stddef.h>

#include <vga.h>

void term_init(void);
void term_clear(void);
void term_scroll(int lines);
void term_set_cursor(size_t x, size_t y);
void term_set_x(size_t c);
void term_set_y(size_t c);
void term_putchar(char c);
char term_getchar(int x, int y);
uint8_t term_getcolor(int x, int y);
void term_putcolor(int x, int y, uint8_t color);
void term_write(const char *data, size_t size);
void term_writestring(const char *data);
void term_writestringln(const char *data);
void term_set_color(enum vga_color fg, enum vga_color bg);
uint8_t term_get_color_entry(void);
void term_reset_color(void);
uint8_t term_get_default_color(void);
void term_set_color_entry(uint8_t color);
void term_delete(void);
void term_cursor_back(bool delete);
void term_cursor_forward(void);
void term_update_cursor(void);
void term_newline(void);
void term_print_centered(const char *text);
void term_chartest(void);