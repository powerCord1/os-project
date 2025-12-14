#include <stdio.h>

#include <framebuffer.h>

int putchar(int ic)
{
    if (fb_is_initialised) {
        char c = (char)ic;
        fb_putchar(c);
        return ic;
    }
    return 0;
}