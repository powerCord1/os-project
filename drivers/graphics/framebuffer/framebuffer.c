#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <font.h>
#include <framebuffer.h>
#include <image.h>
#include <keyboard.h>
#include <limine.h>
#include <limine_defs.h>
#include <math.h>
#include <panic.h>
#include <sound.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>

#define DEFAULT_TITLE_HEIGHT 3
#define OPTIMISE_FB false

struct limine_framebuffer *fb;
volatile uint32_t *fb_ptr;
static size_t pitch_in_pixels;

uint32_t fg;
uint32_t bg;

const uint32_t default_fg = 0xFFFFFF;
const uint32_t default_bg = 0x000000;

cursor_t cursor;

static bool overwrite_mode = false;
bool fb_is_initialised = false;

void fb_init()
{
    log_verbose("Checking for framebuffers");
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        panic("No framebuffers found.");
    } else {
        log_verbose("Found %d framebuffers",
                    framebuffer_request.response->framebuffer_count);
    }

    fb = framebuffer_request.response->framebuffers[0];
    fb_ptr = fb->address;
    pitch_in_pixels = fb->pitch / 4;

    fg = 0xFFFFFF;
    bg = 0x000000;

    cursor.x = 0;
    cursor.y = 0;
    cursor.visible = false;

    fb_is_initialised = true;
    fb_clear();
    printf("Booting...\n\n");
    print_build_info();
    printf("\n");
}

struct limine_framebuffer *get_fb_data()
{
    return fb;
}

void fb_draw_cursor()
{
    for (uint32_t i = 0; i < char_width; i++) {
        fb_put_pixel(cursor.x + i, cursor.y + char_height - 1, fg);
    }
}

void fb_erase_cursor()
{
    for (uint32_t i = 0; i < char_width; i++) {
        fb_put_pixel(cursor.x + i, cursor.y + char_height - 1, bg);
    }
}

void fb_show_cursor()
{
    cursor.visible = true;
    fb_draw_cursor();
}

void fb_hide_cursor()
{
    cursor.visible = false;
    fb_erase_cursor();
}

void fb_set_cursor(uint32_t x, uint32_t y)
{
    fb_erase_cursor();
    cursor.x = x;
    cursor.y = y;
    if (cursor.visible) {
        fb_draw_cursor();
    }
}

void fb_set_color(uint32_t _fg, uint32_t _bg)
{
    fg = _fg;
    bg = _bg;
}

void fb_set_fg(uint32_t _fg)
{
    fg = _fg;
}

void fb_set_bg(uint32_t _bg)
{
    bg = _bg;
}

void fb_reset_color()
{
    fg = default_fg;
    bg = default_bg;
}

void bell()
{
    request_beep(BELL_FREQ);
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
#if !OPTIMISE_FB
    if (x >= fb->width || y >= fb->height) {
        log_warn(
            "tried to plot pixel outside of framebuffer boundaries: x=%d, y=%d",
            x, y);
        return;
    }
#endif
    fb_ptr[y * pitch_in_pixels + x] = color;
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y)
{
    return fb_ptr[y * pitch_in_pixels + x];
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color)
{
    for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
            fb_put_pixel(x + j, y + i, color);
        }
    }
}

void fb_draw_line(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                  uint32_t end_y)
{
    int dx = abs((int)end_x - (int)start_x);
    int dy = abs((int)end_y - (int)start_y);
    int sx = (start_x < end_x) ? 1 : -1;
    int sy = (start_y < end_y) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        fb_put_pixel(start_x, start_y, fg);

        if (start_x == end_x && start_y == end_y) {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            start_x += sx;
        }
        if (e2 < dx) {
            err += dx;
            start_y += sy;
        }
    }
    fb_put_pixel(end_x, end_y, fg);
}

void fb_draw_arc(uint32_t center_x, uint32_t center_y, int radius,
                 int start_angle, int end_angle, uint32_t color)
{
    int x = radius, y = 0;
    int err = 1 - radius;

    while (x >= y) {
        if (start_angle == 0 && end_angle == 360) {
            fb_put_pixel(center_x + x, center_y + y, color);
            fb_put_pixel(center_x + y, center_y + x, color);
            fb_put_pixel(center_x - y, center_y + x, color);
            fb_put_pixel(center_x - x, center_y + y, color);
            fb_put_pixel(center_x - x, center_y - y, color);
            fb_put_pixel(center_x - y, center_y - x, color);
            fb_put_pixel(center_x + y, center_y - x, color);
            fb_put_pixel(center_x + x, center_y - y, color);
        }
        // TODO: for partial arcs, calculate the angle of the current (x, y)
        // and check if it's in the desired range before plotting.

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void fb_draw_circle(uint32_t center_x, uint32_t center_y, int radius,
                    uint32_t color, bool filled)
{
    if (filled) {
        for (int y = -radius; y <= radius; y++) {
            for (int x = -radius; x <= radius; x++) {
                if (x * x + y * y <= radius * radius) {
                    uint32_t draw_x = center_x + x;
                    uint32_t draw_y = center_y + y;
                    if (draw_x < fb->width && draw_y < fb->height) {
                        fb_put_pixel(draw_x, draw_y, color);
                    }
                }
            }
        }
    } else {
        fb_draw_arc(center_x, center_y, radius, 0, 360, color);
    }
}

void fb_draw_image(image_t *image, uint32_t x, uint32_t y)
{
    if (image->width + x > fb->width || image->height + y > fb->height) {
        log_err("image too large for framebuffer");
        return;
    }

    for (uint32_t i = 0; i < image->height; i++) {
        for (uint32_t j = 0; j < image->width; j++) {
            uint32_t pixel_color =
                ((uint32_t *)image->data)[i * image->width + j];
            fb_put_pixel(x + j, y + i, pixel_color);
        }
    }
}

void fb_clear()
{
    fb_fill_screen(0);
    cursor.x = 0;
    cursor.y = 0;
}

void fb_clear_region(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                     uint32_t end_y)
{
    fb_draw_rect(start_x, start_y, end_x - start_x, end_y - start_y, bg);
}

void fb_clear_line(uint32_t line_num)
{
    fb_draw_rect(0, line_num * char_height, fb->width, char_height, bg);
}

void fb_clear_vp()
{
    const uint32_t title_height_pixels = char_height * DEFAULT_TITLE_HEIGHT;
    fb_clear_region(0, title_height_pixels, fb->width,
                    fb->height - title_height_pixels);
    fb_set_cursor(0, title_height_pixels + char_height);
}

bool fb_is_region_empty(uint32_t start_x, uint32_t start_y, uint32_t end_x,
                        uint32_t end_y)
{
    for (uint32_t y = start_y; y < end_y; y++) {
        for (uint32_t x = start_x; x < end_x; x++) {
            if (fb_get_pixel(x, y) != bg) {
                return false;
            }
        }
    }
    return true;
}

void fb_backspace()
{
    fb_erase_cursor();

    if (cursor.x == 0) {
        if (cursor.y == 0) {
            bell();
            fb_draw_cursor();
            return;
        }
        // TODO: handle backspace at the beginning of a line to wrap to the
        // previous line.
        bell();
        fb_draw_cursor();
        return;
    }

    cursor.x -= char_width;

    if (!fb_is_region_empty(cursor.x + char_width, cursor.y, fb->width,
                            cursor.y + char_height)) {
        log_kbd_action("shifting characters left");
        for (uint32_t y = cursor.y; y < cursor.y + char_height; y++) {
            memmove(&fb_ptr[y * pitch_in_pixels + cursor.x],
                    &fb_ptr[y * pitch_in_pixels + cursor.x + char_width],
                    (fb->width - cursor.x - char_width) * sizeof(uint32_t));
        }

        fb_draw_rect(fb->width - char_width, cursor.y, char_width, char_height,
                     bg);

        fb_draw_cursor();
    } else {
        fb_clear_region(cursor.x, cursor.y, cursor.x + char_width,
                        cursor.y + char_height);
    }
}

void fb_delete()
{
    fb_cursor_right();
    fb_backspace();
}

void fb_cursor_left()
{
    uint32_t new_x = cursor.x;
    uint32_t new_y = cursor.y;
    if (cursor.x == 0) {
        if (cursor.y == 0) {
            bell();
            return;
        } else {
            new_y -= char_height;
            new_x = fb->width - char_width;
        }
    } else {
        new_x -= char_width;
    }
    fb_set_cursor(new_x, new_y);
}

void fb_cursor_right()
{
    uint32_t new_x = cursor.x;
    uint32_t new_y = cursor.y;
    if (cursor.x == fb->width - char_width) {
        if (cursor.y == fb->height - char_height) {
            bell();
            return;
        } else {
            new_y += char_height;
            new_x = 0;
        }
    } else {
        new_x += char_width;
    }
    fb_set_cursor(new_x, new_y);
}

void fb_cursor_up()
{
    if (cursor.y == 0) {
        bell();
        return;
    } else {
        fb_set_cursor(cursor.x, cursor.y - char_height);
    }
}

void fb_cursor_down()
{
    if (cursor.y == fb->height - char_height) {
        bell();
        return;
    } else {
        fb_set_cursor(cursor.x, cursor.y + char_height);
    }
}

void fb_cursor_home()
{
    fb_set_cursor(0, cursor.y);
}

void fb_cursor_end()
{
    fb_set_cursor(fb->width - char_width, cursor.y);
}

void fb_newline()
{
    uint32_t new_x = 0;
    uint32_t new_y = cursor.y + char_height;
    if (new_y >= fb->height - char_height) {
        fb_scroll();
        new_y -= char_height;
    }
    fb_set_cursor(new_x, new_y);
}

void fb_scroll()
{
    bool was_cursor_visible = cursor.visible;
    if (was_cursor_visible) {
        fb_erase_cursor(); // don't copy the cursor
    }
    memmove(fb_ptr, fb_ptr + char_height * pitch_in_pixels,
            (fb->height - char_height) * fb->pitch);

    for (uint32_t y = fb->height - char_height; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            fb_put_pixel(x, y, bg);
        }
    }
    if (was_cursor_visible) {
        fb_draw_cursor();
    }
}

void fb_fill_screen(uint32_t color)
{
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            fb_put_pixel(x, y, color);
        }
    }
}

void fb_matrix_test()
{
    for (size_t i = 0; i < fb->height; i++) {
        for (size_t j = 0; j < fb->width; j++) {
            if ((j & 1) == 0 && (i & 1) == 1) {
                fb_put_pixel(j, i, 0xFFFFFF);
            }
        }
    }
}

void fb_char_test()
{
    for (uint8_t i = 0; i < 255; i++) {
        putchar(i);
    }
}

void fb_rgb_test()
{
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            double h = (double)x / fb->width * 360.0;
            double s = 1.0;
            double v = 1.0;

            // convert to RGB
            double c = v * s;
            double x_prime = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
            double m = v - c;
            double r_prime, g_prime, b_prime;

            if (0 <= h && h < 60) {
                r_prime = c;
                g_prime = x_prime;
                b_prime = 0;
            } else if (60 <= h && h < 120) {
                r_prime = x_prime;
                g_prime = c;
                b_prime = 0;
            } else if (120 <= h && h < 180) {
                r_prime = 0;
                g_prime = c;
                b_prime = x_prime;
            } else if (180 <= h && h < 240) {
                r_prime = 0;
                g_prime = x_prime;
                b_prime = c;
            } else if (240 <= h && h < 300) {
                r_prime = x_prime;
                g_prime = 0;
                b_prime = c;
            } else { // 300 <= h && h < 360
                r_prime = c;
                g_prime = 0;
                b_prime = x_prime;
            }

            uint8_t r = (uint8_t)((r_prime + m) * 255);
            uint8_t g = (uint8_t)((g_prime + m) * 255);
            uint8_t b = (uint8_t)((b_prime + m) * 255);

            fb_put_pixel(x, y, (r << 16) | (g << 8) | b);
        }
    }
}

void fb_putchar(char c)
{
    if (c == '\n') {
        overwrite_mode = false;
        fb_newline();
        return;
    } else if (c == '\b') {
        overwrite_mode = false;
        fb_backspace();
        return;
    } else if (c == '\r') {
        overwrite_mode = true;
        fb_cursor_home();
        return;
    } else if (c == '\t') {
        for (int i = 0; i < INDENT_WIDTH; i++) {
            fb_putchar(' ');
        }
        return;
    } else if (c == 127) { // ASCII DEL
        fb_delete();
        return;
    }

    fb_erase_cursor();

    if (!overwrite_mode &&
        !fb_is_region_empty(
            cursor.x, cursor.y, cursor.x + char_width, // is there a character
            cursor.y + char_height)) { // in front of the cursor? this is ugly,
                                       // make a text buffer at some point
        log_kbd_action("shifting characters right");
        for (uint32_t y = cursor.y; y < cursor.y + char_height; y++) {
            memmove(&fb_ptr[y * pitch_in_pixels + cursor.x + char_width],
                    &fb_ptr[y * pitch_in_pixels + cursor.x],
                    (fb->width - cursor.x - char_width) * sizeof(uint32_t));
        }
    } else {
        // A printable character is being written, disable overwrite mode.
        overwrite_mode = false;
    }

    fb_putchar_at(c, cursor.x, cursor.y);
    cursor.x += char_width;
    if (cursor.x >= fb->width) {
        cursor.x = 0;
        cursor.y += char_height;
    }
    if (cursor.y >= fb->height) {
        fb_scroll();
        cursor.y -= char_height;
    }
    fb_draw_cursor();
}

void fb_putchar_at(char c, uint32_t x_pos, uint32_t y_pos)
{
    uint8_t *glyph = get_font_glyph(c);
    if (!glyph) {
        // draw hollow rectangle for missing characters
        fb_draw_rect(x_pos, y_pos, char_width, char_height, bg);
        return;
    }

    for (uint32_t y = 0; y < char_height; y++) {
        for (uint32_t x = 0; x < char_width; x++) {
            if ((glyph[y] >> (char_width - 1 - x)) & 1) {
                fb_put_pixel(x_pos + x, y_pos + y, fg);
            } else {
                fb_put_pixel(x_pos + x, y_pos + y, bg);
            }
        }
    }
}

void fb_print_string(const char *str)
{
    for (size_t i = 0; i < strlen(str); i++) {
        fb_putchar(str[i]);
    }
}

void fb_print_centered(const char *text)
{
    size_t text_width_pixels = strlen(text) * char_width;
    cursor.x = (fb->width - text_width_pixels) / 2;
    fb_print_string(text);
}

void fb_draw_title(const char *title)
{
    fb_hide_cursor();

    fb_set_color(0, 0xffffff);

    // draw white block
    fb_draw_rect(0, 0, fb->width, char_height * DEFAULT_TITLE_HEIGHT, 0xffffff);

    // draw text in middle of block
    fb_set_cursor(0, DEFAULT_TITLE_HEIGHT / 2 * char_height);
    fb_print_centered(title);

    // set cursor after title
    fb_set_cursor(0, char_height * (DEFAULT_TITLE_HEIGHT + 1));

    // restore settings
    fb_reset_color();
}

void wait_for_render()
{
    wait_ms(100);
}