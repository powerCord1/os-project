#pragma once

#include <stddef.h>

void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memcpy(void *__restrict dest, const void *__restrict src, size_t n);
void *memset(void *s, int c, size_t n);

size_t strlen(const char *str);
char *strcpy(char *dest, const char *src);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
int strcmp(const char *s1, const char *s2);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
size_t strspn(const char *str, const char *accept);
char *strpbrk(const char *str, const char *accept);

char *strncpy(char *dest, const char *src, size_t n);
char *strncat(char *dest, const char *src, size_t n);
size_t strnlen(const char *s, size_t maxlen);
char *strcat(char *dest, const char *src);
int strncmp(const char *s1, const char *s2, size_t n);

unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);

char *strstr(const char *haystack, const char *needle);

long strtol(const char *nptr, char **endptr, int base);

void qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

char *itohexa(char *dest, unsigned x);
char *itoa(char *dest, int n);
char *uitoa(char *dest, unsigned int n);
int atoi(const char *str);