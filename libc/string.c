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

char *itoa(char *dest, int n)
{
    char *ptr = dest;
    if (n < 0) {
        *ptr++ = '-';
        n = -n;
    }

    int num_digits = 0;
    int temp = n;
    do {
        num_digits++;
        temp /= 10;
    } while (temp > 0);

    if (n == 0) {
        num_digits = 1;
    }

    ptr += num_digits;
    *ptr-- = '\0';

    do {
        *ptr-- = (n % 10) + '0';
        n /= 10;
    } while (n > 0);

    return dest;
}

static char *uitoa_helper(char *dest, unsigned int n)
{
    if (n >= 10) {
        dest = uitoa_helper(dest, n / 10);
    }
    *dest++ = (n % 10) + '0';
    return dest;
}

char *uitoa(char *dest, unsigned int n)
{
    *uitoa_helper(dest, n) = '\0';
    return dest;
}