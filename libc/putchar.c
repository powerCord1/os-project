#include <stdio.h>

#include <tty.h>

int putchar(int ic) {
    char c = (char)ic;
    term_putchar(c);
    return ic;
}