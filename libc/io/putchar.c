#include <stdio.h>

#include <framebuffer.h>

int putchar(int ic)
{
    char c = (char)ic;
    fb_putchar(c);
    return ic;
}