#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <debug.h>
#include <serial.h>
#include <stdio.h>
#include <tty.h>

static void write_log(uint8_t type, const char *color, const char *format,
                      va_list args)
{
    if (type <= LOGLEVEL) {
        serial_printf("%s%s%s: ", color, get_log_text(type), ansi_color.nc);
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

void log_debug(const char *format, ...)
{
#if DEBUG
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_INFO, ansi_color.purple, format, args);
    va_end(args);
#endif
}

void log_verbose(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_VERBOSE, ansi_color.light_blue, format, args);
    va_end(args);
}

void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_INFO, ansi_color.blue, format, args);
    va_end(args);
}

void log_warn(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_WARN, ansi_color.yellow, format, args);
    va_end(args);
}

void log_err(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log(LOGLEVEL_ERROR, ansi_color.red, format, args);
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