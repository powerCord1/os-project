#include <stddef.h>

int memcmp(const void *, const void *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
char *strcpy(char *test, const char *src);
int strcmp(const char *s1, const char *s2);
char *strtok(char *str, const char *delim);
char *strpbrk(const char *str, const char *accept);
size_t strspn(const char *str, const char *accept);
size_t strlen(const char *str);
char *strdup(const char *s);

char *itoa(char *dest, int n);
char *uitoa(char *dest, unsigned int n);
char *itohexa(char *dest, unsigned x);