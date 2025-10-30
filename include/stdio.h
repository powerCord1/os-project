#include <stdarg.h>

int printf(const char *__restrict, ...);
int putchar(int ic);
int vprintf(const char *restrict format, va_list parameters);
int vprintf_generic(int (*putc_func)(int), const char *restrict format,
                    va_list parameters);