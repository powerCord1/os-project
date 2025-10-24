#include <stdbool.h>
#include <stdint.h>

#include <debug.h>
#include <stdio.h>
#include <tty.h>

static void write_log(uint8_t type, enum vga_color color, const char *msg,
                      bool coloredMsg)
{
    if (type <= LOGLEVEL) {
        term_set_color(color, VGA_COLOR_BLACK);
        term_writestring(get_log_text(type));
        if (!coloredMsg) {
            term_reset_color();
        }
        term_writestring(": ");
        term_writestringln(msg);
        if (coloredMsg) {
            term_reset_color();
        }
    }
}

const char *get_log_text(uint8_t type)
{
    switch (type) {
    case 1:
        return "error";
        break;

    case 2:
        return "warning";
        break;

    case 3:
        return "info";
        break;

    case 4:
        return "verbose";
        break;
    default:
        return "UNKNOWN LOGLEVEL";
        break;
    }
}

void log_verbose(const char *msg)
{
    write_log(LOGLEVEL_VERBOSE, VGA_COLOR_LIGHT_BLUE, msg, false);
}

void log_info(const char *msg)
{
    write_log(LOGLEVEL_INFO, VGA_COLOR_BLUE, msg, false);
}

void log_warn(const char *msg)
{
    write_log(LOGLEVEL_WARN, VGA_COLOR_BROWN, msg, false);
}

void log_err(const char *msg)
{
    write_log(LOGLEVEL_ERROR, VGA_COLOR_RED, msg, true);
}

void breakpoint()
{
    __asm__ volatile("1: jmp 1b");
}