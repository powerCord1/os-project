#include <debug.h>
#include <stdio.h>
#include <tty.h>

static void write_log(const char* type, enum vga_color color, const char* msg) {
    putchar("[");
    term_set_color(color, VGA_COLOR_BLACK);
    term_writestring(type);
    term_writestring("]: ");
    term_writestringln(msg);
}

void log_warning(const char* msg) {
    write_log("warning", VGA_COLOR_BROWN, msg);
}

void log_info(const char* msg) {
    write_log("info", VGA_COLOR_BROWN, msg);
}

void log_verbose(const char* msg) {
    printf("[verbose]: %s", msg);
}