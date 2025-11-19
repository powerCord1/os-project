#include <stdint.h>

extern struct limine_framebuffer *fb;
extern volatile uint32_t *fb_ptr;

void fb_init();
void fb_clear();
void fb_fill_screen(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_putchar(char c);
void fb_putchar_at(char c, uint32_t x_pos, uint32_t y_pos);
void fb_print_string(const char *str);
void fb_matrix_test();
void fb_draw_title(const char *title);