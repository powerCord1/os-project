#include <stddef.h>
#include <stdint.h>

#include <string.h>

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

static char *itohexa_helper(char *dest, unsigned x)
{
    if (x >= 16) {
        dest = itohexa_helper(dest, x / 16);
    }
    *dest++ = "0123456789abcdef"[x & 15];
    return dest;
}

char *itohexa(char *dest, unsigned x)
{
    *itohexa_helper(dest, x) = '\0';
    return dest;
}