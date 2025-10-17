#include <stdbool.h>

#include <debug.h>
#include <stdio.h>
#include <tty.h>

static void write_log(const char* type, enum vga_color color, const char* msg, bool inBrackets, bool coloredMsg) {
    if (inBrackets) putchar('[');
    term_set_color(color, VGA_COLOR_BLACK);
    term_writestring(type);
    if (!coloredMsg) term_reset_color();
    if (inBrackets) term_writestring("]");
    term_writestring(": ");
    term_writestringln(msg);
    if (coloredMsg) term_reset_color();
}

void log_verbose(const char* msg) {
    write_log("verbose", VGA_COLOR_LIGHT_BLUE, msg, true, false);
}

void log_info(const char* msg) {
    write_log("info", VGA_COLOR_BLUE, msg, true, false);
}

void log_warning(const char* msg) {
    write_log("warning", VGA_COLOR_BROWN, msg, false, false);
}

void log_error(const char* msg) {
    write_log("error", VGA_COLOR_RED, msg, false, true);
}