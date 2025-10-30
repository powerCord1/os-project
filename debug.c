#include <stdbool.h>
#include <stdint.h>

#include <debug.h>
#include <serial.h>
#include <stdio.h>
#include <tty.h>

const char *ansi_color[] = {
    [ANSI_COLOR_NC] = "\033[0m",
    [ANSI_COLOR_BLACK] = "\033[0;30m",
    [ANSI_COLOR_RED] = "\033[0;31m",
    [ANSI_COLOR_GREEN] = "\033[0;32m",
    [ANSI_COLOR_BROWN] = "\033[0;33m",
    [ANSI_COLOR_BLUE] = "\033[0;34m",
    [ANSI_COLOR_PURPLE] = "\033[0;35m",
    [ANSI_COLOR_CYAN] = "\033[0;36m",
    [ANSI_COLOR_LIGHT_GREY] = "\033[0;37m",
    [ANSI_COLOR_DARK_GREY] = "\033[1;30m",
    [ANSI_COLOR_LIGHT_RED] = "\033[1;31m",
    [ANSI_COLOR_LIGHT_GREEN] = "\033[1;32m",
    [ANSI_COLOR_YELLOW] = "\033[1;33m",
    [ANSI_COLOR_LIGHT_BLUE] = "\033[1;34m",
    [ANSI_COLOR_LIGHT_PURPLE] = "\033[1;35m",
    [ANSI_COLOR_LIGHT_CYAN] = "\033[1;36m",
    [ANSI_COLOR_WHITE] = "\033[1;37m",
};

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

    case 2:
        return "warning";

    case 3:
        return "info";

    case 4:
        return "verbose";
    default:
        return "UNKNOWN LOGLEVEL";
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
    serial_writestringln("---LOG TEST---");
    log_err("error");
    log_warn("warning");
    log_info("info");
    log_verbose("verbose");
}

void breakpoint()
{
    __asm__ volatile("1: jmp 1b");
}