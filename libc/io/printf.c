#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <string.h>
#include <tty.h>

static bool print(int (*putc_func)(int), const char *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < length; i++) {
        if (putc_func(bytes[i]) == -1) {
            return false;
        }
    }
    return true;
}

static char *ultohexa_helper(char *dest, unsigned long x)
{
    if (x >= 16) {
        dest = ultohexa_helper(dest, x / 16);
    }
    *dest++ = "0123456789abcdef"[x & 15];
    return dest;
}

static char *ultohexa(char *dest, unsigned long x)
{
    if (x == 0) {
        strcpy(dest, "0");
        return dest;
    }
    *ultohexa_helper(dest, x) = '\0';
    return dest;
}

int vprintf_generic(int (*putc_func)(int),
                    const char *restrict format, // NOLINT
                    va_list parameters)
{
    int written = 0;

    while (*format != '\0') {
        size_t maxrem = INT_MAX - written;

        if (format[0] != '%' || format[1] == '%') {
            if (format[0] == '%') {
                format++;
            }
            size_t amount = 1;
            while (format[amount] && format[amount] != '%') {
                amount++;
            }
            if (maxrem < amount) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(putc_func, format, amount)) {
                return -1;
            }
            format += amount;
            written += amount;
            continue;
        }

        const char *format_begun_at = format++;

        bool zero_pad = false;
        int width = 0;
        bool is_long = false;

        if (*format == 'c') {
            format++;
            char c = (char)va_arg(parameters, int /* char promotes to int */);
            if (!maxrem) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (putc_func(c) == -1) {
                return -1;
            }
            written++;
            continue;
        } else if (*format == 's') {
            format++;
            const char *str = va_arg(parameters, const char *);
            size_t len = strlen(str);
            if (maxrem < len) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(putc_func, str, len)) {
                return -1;
            }
            written += len;
            continue;
        } else if (*format == 'x') {
            // This case is now handled by the more generic parser below
            // to support flags and width, so we just fall through.
        }

        if (*format == '0') {
            zero_pad = true;
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == 'l') {
            is_long = true;
            format++;
        }

        if (*format == 'x') {
            format++;
            char hex_str[20]; // 16 hex chars for 64-bit + null terminator
            if (is_long) {
                unsigned long i = va_arg(parameters, unsigned long);
                ultohexa(hex_str, i);
            } else {
                unsigned int i = va_arg(parameters, unsigned int);
                itohexa(hex_str, i);
            }
            size_t len = strlen(hex_str);
            int padding = (width > (int)len) ? (width - len) : 0;

            if (maxrem < len + padding) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            for (int i = 0; i < padding; i++) {
                putc_func(zero_pad ? '0' : ' ');
            }
            print(putc_func, hex_str, len);
            written += len + padding;
            continue;
        } else if (*format == 'd') {
            format++;
            int i = va_arg(parameters, int);
            char str[12]; // Max 11 chars for a 32-bit signed int + null
            itoa(str, i);
            size_t len = strlen(str);
            int padding = (width > (int)len) ? (width - len) : 0;

            if (maxrem < len + padding) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            for (int j = 0; j < padding; j++) {
                putc_func(zero_pad ? '0' : ' ');
            }
            if (!print(putc_func, str, len)) {
                return -1;
            }
            written += len + padding;
            continue;
        } else if (*format == 'u') {
            format++;
            unsigned int i = va_arg(parameters, unsigned int);
            char str[11]; // Max 10 chars for a 32-bit unsigned int + null
            uitoa(str, i);
            size_t len = strlen(str);
            int padding = (width > (int)len) ? (width - len) : 0;

            if (maxrem < len + padding) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            for (int j = 0; j < padding; j++) {
                putc_func(zero_pad ? '0' : ' ');
            }
            if (!print(putc_func, str, len)) {
                return -1;
            }
            written += len + padding;
            continue;
        } else {
            format = format_begun_at;
            size_t len = strlen(format);
            if (maxrem < len) {
                // TODO: Set errno to EOVERFLOW.
                return -1;
            }
            if (!print(putc_func, format, len)) {
                return -1;
            }
            written += len;
            format += len;
        }
    }

    return written;
}

int vprintf(const char *restrict format, va_list parameters)
{
    return vprintf_generic(putchar, format, parameters);
}

int printf(const char *restrict format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    int written = vprintf(format, parameters);
    va_end(parameters);
    return written;
}