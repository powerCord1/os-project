#include <stdbool.h>
#include <stdint.h>

#include <debug.h>
#include <serial.h>
#include <stdio.h>
#include <tty.h>

static void write_log(uint8_t type, const char *color, const char *msg)
{
    if (type <= LOGLEVEL) {
        serial_writestring(color);
        serial_writestring(get_log_text(type));
        serial_writestring(ansi_color[ANSI_COLOR_NC]);
        serial_writestring(": ");
        serial_writestringln(msg);
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
    write_log(LOGLEVEL_VERBOSE, ansi_color[ANSI_COLOR_LIGHT_BLUE], msg);
}

void log_info(const char *msg)
{
    write_log(LOGLEVEL_INFO, ansi_color[ANSI_COLOR_BLUE], msg);
}

void log_warn(const char *msg)
{
    write_log(LOGLEVEL_WARN, ansi_color[ANSI_COLOR_YELLOW], msg);
}

void log_err(const char *msg)
{
    write_log(LOGLEVEL_ERROR, ansi_color[ANSI_COLOR_RED], msg);
}

void log_test()
{
    log_err("error");
    log_warn("warning");
    log_info("info");
    log_verbose("verbose");
}

void breakpoint()
{
    __asm__ volatile("1: jmp 1b");
}