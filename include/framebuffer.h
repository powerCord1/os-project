#include <stdbool.h>
#include <stdint.h>

#include <image.h>

#define BELL_FREQ 4750
#define INDENT_WIDTH 4

extern struct limine_framebuffer *fb;
extern volatile uint32_t *fb_ptr;

typedef struct {
    uint32_t x;
    uint32_t y;
    bool visible;
} cursor_t;

void fb_init();
struct limine_framebuffer *get_fb_data();
void bell();
void fb_clear();
void fb_clear_region(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                     uint32_t end_y);
void fb_clear_line(uint32_t line_num);
void fb_clear_vp();
bool fb_is_region_empty(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                        uint32_t end_y);
void fb_set_color(uint32_t _fg, uint32_t _bg);
void fb_set_fg(uint32_t _fg);
void fb_set_bg(uint32_t _bg);
void fb_reset_color();
void fb_fill_screen(uint32_t color);
void fb_scroll();
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(uint32_t x, uint32_t y);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color);
void fb_draw_line(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                  uint32_t end_y);
void fb_draw_circle(uint32_t center_x, uint32_t center_y, int radius,
                    uint32_t color, bool filled);
void fb_draw_image(image_t *image, uint32_t x, uint32_t y);
void fb_putchar(char c);
void fb_putchar_at(char c, uint32_t x_pos, uint32_t y_pos);
void fb_print_string(const char *str);
void fb_matrix_test();
void fb_char_test();
void fb_rgb_test();
void fb_draw_title(const char *title);
void fb_newline();
void fb_backspace();
void fb_delete();
void fb_cursor_left();
void fb_cursor_right();
void fb_cursor_up();
void fb_cursor_down();
void fb_cursor_home();
void fb_cursor_end();
void fb_draw_cursor();
void fb_erase_cursor();
void fb_show_cursor();
void fb_hide_cursor();
void fb_print_centered(const char *text);
void fb_set_cursor(uint32_t x, uint32_t y);