#pragma once

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    int fd;
} FILE;

extern FILE *stdout;
extern FILE *stderr;

int printf(const char *__restrict, ...);
int putchar(int ic);
int vprintf(const char *restrict format, va_list parameters);
int vprintf_generic(int (*putc_func)(int, void *), void *putc_data,
                    const char *restrict format, va_list parameters);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int sprintf(char *str, const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);