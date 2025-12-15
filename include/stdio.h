#include <stdarg.h>
#include <stddef.h>

int printf(const char *__restrict, ...);
int putchar(int ic);
int vprintf(const char *restrict format, va_list parameters);
int vprintf_generic(int (*putc_func)(int, void *), void *putc_data,
                    const char *restrict format, va_list parameters);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);