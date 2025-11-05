#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <math.h>
#include <pit.h>
#include <string.h>
#include <tty.h>
#include <vga.h>

static size_t term_cursor_y;
static size_t term_cursor_x;
static uint8_t term_color;
static uint16_t *term_buffer = (uint16_t *)VGA_MEMORY;

void term_init()
{
    term_cursor_y = 0;
    term_cursor_x = 0;
    term_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    term_clear();
    enable_cursor();
}
void term_clear()
{
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            term_buffer[index] = vga_entry(' ', term_color);
        }
    }
    term_set_cursor(0, 0);
}

void term_set_cursor(size_t x, size_t y)
{
    term_cursor_x = x;
    term_cursor_y = y;
    term_update_cursor();
}

void term_set_x(size_t c)
{
    term_cursor_x = c;
}

void term_set_y(size_t c)
{
    term_cursor_y = c;
}

void term_set_color(enum vga_color fg, enum vga_color bg)
{
    term_color = vga_entry_color(fg, bg);
}

void term_set_color_entry(uint8_t color)
{
    term_color = color;
}

uint8_t term_get_default_color()
{
    return vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void term_reset_color()
{
    term_color = term_get_default_color();
}

uint8_t term_get_color_entry()
{
    return term_color;
}

void term_scroll(int lines)
{
    if (lines <= 0) {
        return;
    }
    if ((size_t)lines >= VGA_HEIGHT) {
        term_clear();
        return;
    }

    memmove(term_buffer, term_buffer + lines * VGA_WIDTH,
            (VGA_HEIGHT - lines) * VGA_WIDTH * sizeof(uint16_t));

    for (size_t y = VGA_HEIGHT - lines; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            term_buffer[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
        }
    }
}

void term_putentryat(char c, uint8_t color, size_t x, size_t y)
{
    const size_t index = y * VGA_WIDTH + x;
    term_buffer[index] = vga_entry(c, color);
}

void term_delete()
{
    if (term_cursor_x >= VGA_WIDTH) {
        return;
    }

    memmove(term_buffer + term_cursor_y * VGA_WIDTH + term_cursor_x,
            term_buffer + term_cursor_y * VGA_WIDTH + term_cursor_x + 1,
            (VGA_WIDTH - term_cursor_x - 1) * sizeof(uint16_t));

    term_putentryat(' ', term_color, VGA_WIDTH - 1, term_cursor_y);
}

void term_cursor_back(bool delete)
{
    if (term_cursor_x > 0) {
        term_cursor_x--;
    } else if (term_cursor_y > 0) {
        term_cursor_y--;
        term_cursor_x = VGA_WIDTH - 1;
    } else {
        // bell at 0,0
        pit_request_beep(1000);
        return;
    }

    if (delete) {
        term_putentryat(' ', term_color, term_cursor_x, term_cursor_y);
    }

    term_update_cursor();
}

void term_cursor_forward()
{
    if (term_cursor_x < VGA_WIDTH - 1) {
        term_cursor_x++;
    } else if (term_cursor_y < VGA_HEIGHT - 1) {
        term_cursor_y++;
        term_cursor_x = 0;
    } else {
        term_scroll(1);
        term_cursor_x = 0;
        return;
    }

    term_update_cursor();
}

void term_cursor_up()
{
    if (term_cursor_y > 0) {
        term_cursor_y--;
    } else {
        pit_request_beep(1000);
        return;
    }
    term_update_cursor();
}

void term_cursor_down()
{
    if (term_cursor_y < VGA_HEIGHT - 1) {
        term_cursor_y++;
    } else {
        pit_request_beep(1000);
        return;
    }
    term_update_cursor();
}

void term_update_cursor()
{
    vga_set_cursor(term_cursor_x, term_cursor_y);
}

void term_putchar(char c)
{
    if (c == '\n') {
        term_newline();
        return;
    } else if (c == '\b') {
        term_cursor_back(true);
        return;
    } else if (c == 127) { // ASCII DEL
        term_delete();
        return;
    }
    term_putentryat(c, term_color, term_cursor_x, term_cursor_y);
    if (++term_cursor_x >= VGA_WIDTH) {
        term_newline();
    }
    term_update_cursor();
}

char term_getchar(int x, int y)
{
    return term_buffer[2 * (y * VGA_WIDTH + x)];
}

uint8_t term_getcolor(int x, int y)
{
    return term_buffer[2 * (y * VGA_WIDTH + x) + 1];
}

void term_putcolor(int x, int y, uint8_t color)
{
    term_buffer[2 * (y * VGA_WIDTH + x) + 1] = color;
}

void term_write(const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        term_putchar(data[i]);
    }
}

void term_writestring(const char *data)
{
    term_write(data, strlen(data));
}

void term_writestringln(const char *data)
{
    term_write(data, strlen(data));
    term_newline();
}

void term_newline()
{
    term_cursor_x = 0;
    term_cursor_y++;
    if (term_cursor_y >= VGA_HEIGHT) {
        term_scroll(1);
        term_cursor_y = VGA_HEIGHT - 1;
    }
    term_update_cursor();
}

void term_print_centered(const char *text)
{
    uint8_t screen_midpoint = round_to_even(VGA_WIDTH / 2, false);
    uint8_t text_midpoint = round_to_even(strlen(text) / 2, false);

    term_cursor_x = screen_midpoint - text_midpoint;
    term_writestring(text);
}

void term_chartest()
{
    for (int i = 0; i < 256; i++) {
        term_putchar(i);
    }
}