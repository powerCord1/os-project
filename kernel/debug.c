#include <stdarg.h>
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

static void write_log(uint8_t type, const char *color, const char *format,
                      va_list args)
{
    if (type <= LOGLEVEL) {
        serial_writestring(color);
        serial_writestring(get_log_text(type));
        serial_writestring(ansi_color[ANSI_COLOR_NC]);
        serial_writestring(": ");
        vserial_printf(format, args);
        serial_writestring("\n");
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

void log_verbose(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_VERBOSE, ansi_color[ANSI_COLOR_LIGHT_BLUE], format,
              args);
    va_end(args);
}

void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_INFO, ansi_color[ANSI_COLOR_BLUE], format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_WARN, ansi_color[ANSI_COLOR_YELLOW], format, args);
    va_end(args);
}

void log_err(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_ERROR, ansi_color[ANSI_COLOR_RED], format, args);
    va_end(args);
}

void log_test()
{
    serial_writestringln("--- LOG TEST ---");
    log_err("error with a number: %d", -123);
    log_warn("warning with a string: %s", "test string");
    log_info("info with unsigned int: %u", 123);
    log_verbose("verbose info with hex: 0x%x", 0xDEADBEEF);
}

void breakpoint()
{
    __asm__ volatile("1: jmp 1b");
}