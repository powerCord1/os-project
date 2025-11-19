#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <font.h>
#include <framebuffer.h>
#include <limine.h>
#include <limine_defs.h>
#include <panic.h>
#include <stdio.h>
#include <string.h>

struct limine_framebuffer *fb;
volatile uint32_t *fb_ptr;

uint32_t fg;
uint32_t bg;

uint32_t cursor_y;
uint32_t cursor_x;

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

    cursor_y = 0;
    cursor_x = 0;

    fb_clear();
    putchar(1); // smiley face
    printf("string!");
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    fb_ptr[y * (fb->pitch / 4) + x] = color;
}

void fb_clear()
{
    fb_fill_screen(0);
    cursor_x = 0;
    cursor_y = 0;
}

void fb_newline()
{
    cursor_x = 0;
    cursor_y += char_height;
    if (cursor_y >= fb->height) {
        cursor_y = 0; // TODO: implement scrolling
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

void fb_putchar(char c)
{
    if (c == '\n') {
        fb_newline();
        return;
    } else if (c == '\b') {
        // fb_cursor_back(true);
        return;
    } else if (c == 127) { // ASCII DEL
        // fb_delete();
        return;
    }

    fb_putchar_at(c, cursor_x, cursor_y);
    cursor_x += char_width;
    if (cursor_x >= fb->width) {
        cursor_x = 0;
        cursor_y += char_height;
    }
    if (cursor_y >= fb->height) {
        cursor_y = 0; // TODO: implement scrolling
    }
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

void fb_draw_title(const char *title)
{
    // uint8_t height = 3;

    // // draw white block
    // term_set_cursor(0, 0);
    // for (size_t i = 0; i < height * VGA_WIDTH; i++) {
    //     term_set_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    //     term_putchar(' ');
    // }

    // // draw text in middle of block
    // term_set_cursor(0, floordiv2(height));
    // term_print_centered(text);

    // // set cursor after title
    // term_set_cursor(0, height + 1);

    // // restore settings
    // term_reset_color();
}