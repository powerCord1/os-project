#pragma once

#define WASM_IMPORT __attribute__((import_module("env"), import_name(#name)))

extern void print(const char *ptr, int len)
    __attribute__((import_module("env"), import_name("print")));
extern void putchar(int c)
    __attribute__((import_module("env"), import_name("putchar")));
extern unsigned long long get_ticks(void)
    __attribute__((import_module("env"), import_name("get_ticks")));

static int strlen(const char *s)
{
    int len = 0;
    while (s[len])
        len++;
    return len;
}

static void puts(const char *s)
{
    print(s, strlen(s));
}

static void print_num(int n)
{
    char buf[12];
    int i = 0;
    int neg = 0;

    if (n < 0) {
        neg = 1;
        n = -n;
    }
    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    if (neg)
        buf[i++] = '-';

    char rev[12];
    for (int j = 0; j < i; j++)
        rev[j] = buf[i - 1 - j];

    print(rev, i);
}
