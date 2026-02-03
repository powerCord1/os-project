#include <stdbool.h>
#include <stdint.h>

#include <image.h>

// Frequency of terminal bell
#define BELL_FREQ 4750

// Number of spaces that are inserted when pressing 'TAB'
#define INDENT_WIDTH 4

extern struct limine_framebuffer *fb;

// Pointer to the Limine framebuffer array
extern volatile uint32_t *fb_ptr;
extern bool fb_is_initialised;

// The framebuffer cursor
typedef struct {
    uint32_t x;
    uint32_t y;
    bool visible;
} cursor_t;

// Initialise the framebuffer
void fb_init();
struct limine_framebuffer *get_fb_data();

// Beep
void bell();

// Clear the whole framebuffer
void fb_clear();

// Clear a section of the framebuffer
void fb_clear_region(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                     uint32_t end_y);

// Clear everything at the specified line
void fb_clear_line(uint32_t line_num);

// Clear the viewport
void fb_clear_vp();

// Test if every pixel in a specified region is the background colour
bool fb_is_region_empty(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                        uint32_t end_y);

// Set the draw colour of text
void fb_set_color(uint32_t _fg, uint32_t _bg);

// Set foreground colour of text
void fb_set_fg(uint32_t _fg);

// Set background colour of text
void fb_set_bg(uint32_t _bg);

// Reset the text colour
void fb_reset_color();

// Fill the screen with a specified colour
void fb_fill_screen(uint32_t color);

// Scroll the framebuffer one line
void fb_scroll();

// Plot a pixel
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);

// Return the colour of a pixel at the specified coordinates
uint32_t fb_get_pixel(uint32_t x, uint32_t y);

// Draw a rectangle
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color);

// Draw a line
void fb_draw_line(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                  uint32_t end_y);

// Draw a circle
void fb_draw_circle(uint32_t center_x, uint32_t center_y, int radius,
                    uint32_t color, bool filled);

// Draw an image
void fb_draw_image(image_t *image, uint32_t x, uint32_t y);

// Insert a character at the current cursor coordinates
void fb_putchar(char c);

// Insert a character at the specified location
void fb_putchar_at(char c, uint32_t x_pos, uint32_t y_pos);

// Write a string at the current cursor coordinates
void fb_print_string(const char *str);

// Draw a checkerboard pattern
void fb_matrix_test();

// Dump all characters; used for testing font.
void fb_char_test();

// Colour test
void fb_rgb_test();

// Draw the page title
void fb_draw_title(const char *title);

// Insert a blank line. Same as putchar("\n")
void fb_newline();

// Delete the character behind the cursor and shift all characters in front of
// the cursor backward
void fb_backspace();

// Delete the character in front the cursor and shift all preceding characters
// backward
void fb_delete();

// Move the cursor left one space
void fb_cursor_left();

// Move the cursor right one space
void fb_cursor_right();

// Move the cursor up one line
void fb_cursor_up();

// Move the cursor down one line
void fb_cursor_down();

// Set the cursor to the start of the line
void fb_cursor_home();

// Set the cursor to the end of the line
void fb_cursor_end();
void fb_draw_cursor();
void fb_erase_cursor();
void fb_show_cursor();
void fb_hide_cursor();

// Print text in the middle of the screen at the cursor x coordinate
void fb_print_centered(const char *text);

// Set the cursor coordinates
void fb_set_cursor(uint32_t x, uint32_t y);

// Wait for framebuffer to render. This is only used during shutdown and reboot,
// when the renderer may not have fully updated the framebuffer before the
// system gets powered down
void wait_for_render();