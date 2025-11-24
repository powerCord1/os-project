#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <font.h>
#include <framebuffer.h>
#include <limine.h>
#include <limine_defs.h>
#include <math.h>
#include <panic.h>
#include <pit.h>
#include <stdio.h>
#include <string.h>

struct limine_framebuffer *fb;
volatile uint32_t *fb_ptr;

uint32_t fg;
uint32_t bg;

const uint32_t default_fg = 0xFFFFFF;
const uint32_t default_bg = 0x000000;

cursor_t cursor;

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

    fg = 0xFFFFFF;
    bg = 0x000000;

    cursor.x = 0;
    cursor.y = 0;
    cursor.visible = false;

    fb_clear();
    printf("Booting...");
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
    pit_request_beep(BELL_FREQ);
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y)
{
    return fb_ptr[y * (fb->pitch / 4) + x];
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

void fb_backspace()
{
    uint32_t new_x = cursor.x;
    uint32_t new_y = cursor.y;
    if (cursor.x == 0) {
        if (cursor.y == 0) {
            bell();
            return;
        }
        new_y -= char_height;
        new_x = fb->width - char_width;
    } else {
        new_x -= char_width;
    }
    fb_set_cursor(new_x, new_y);
    fb_erase_cursor();
    fb_putchar_at(' ', cursor.x, cursor.y);
    if (cursor.visible) {
        fb_draw_cursor();
    }
}

void fb_delete()
{
    // TODO: implement delete function
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
    memmove(fb_ptr, fb_ptr + char_height * (fb->pitch / 4),
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

void fb_putchar(char c)
{
    if (c == '\n') {
        fb_newline();
        return;
    } else if (c == '\b') {
        fb_backspace();
        return;
    } else if (c == '\r') {
        fb_cursor_home();
        return;
    } else if (c == '\t') {
        for (int i = 0; i < INDENT_WIDTH; i++) {
            fb_putchar(' ');
        }
        return;
    } else if (c == 127) { // ASCII DEL
        // fb_delete();
        return;
    }

    fb_erase_cursor();
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
    const unsigned char *char_bitmap = font_bitmap[(unsigned char)c];

    for (uint32_t y = 0; y < char_height; y++) {
        uint8_t row = char_bitmap[y];
        for (uint32_t x = 0; x < char_width; x++) {
            if ((row >> (char_width - 1 - x)) & 1) {
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

    uint8_t height = 3;

    fb_set_color(0, 0xffffff);

    // draw white block
    fb_draw_rect(0, 0, fb->width, char_height * height, 0xffffff);

    // draw text in middle of block
    fb_set_cursor(0, floordiv2(height) * char_height);
    fb_print_centered(title);

    // set cursor after title
    fb_set_cursor(0, char_height * (height + 1));

    // restore settings
    fb_reset_color();
}