#include <stddef.h>
#include <stdint.h>

#include <string.h>

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d == s) {
        return dest;
    }

    if (d < s) {
        // Copy forwards
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        // Copy backwards
        for (size_t i = n; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

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