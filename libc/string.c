#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <heap.h>
#include <string.h>

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d == s) {
        return dest;
    }

    if (d < s) {
        // copy forwards
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        // copy backwards
        for (size_t i = n; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

char *strcpy(char *dest, const char *src)
{
    char *temp = dest;
    while (*src != '\0') {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
    return temp;
}

char *strtok(char *str, const char *delim)
{
    static char *last_token;

    if (str) {
        last_token = str;
    } else if (!last_token) {
        return NULL;
    }

    str = last_token + strspn(last_token, delim);
    if (*str == '\0') {
        last_token = NULL;
        return NULL;
    }

    char *token_end = strpbrk(str, delim);
    if (token_end) {
        *token_end = '\0';
        last_token = token_end + 1;
    } else {
        str = last_token;
        last_token = NULL;
    }

    return str;
}

char *strpbrk(const char *str, const char *accept)
{
    for (; *str; str++) {
        for (const char *a = accept; *a; a++) {
            if (*str == *a) {
                return (char *)str;
            }
        }
    }
    return NULL;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}
size_t strspn(const char *str, const char *accept)
{
    const char *p;
    const char *a;
    size_t count = 0;

    for (p = str; *p != '\0'; ++p) {
        for (a = accept; *a != '\0'; ++a) {
            if (*p == *a) {
                break;
            }
        }
        if (*a == '\0') {
            return count;
        }
        ++count;
    }

    return count;
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

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *new_s = malloc(len);
    if (new_s == NULL) {
        return NULL;
    }
    return strcpy(new_s, s);
}