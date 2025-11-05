#include <stddef.h>

int memcmp(const void *, const void *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *test, const char *src);
int strcmp(const char *s1, const char *s2);
char *strtok(char *str, const char *delim);
char *strpbrk(const char *str, const char *accept);
size_t strspn(const char *str, const char *accept);
size_t strlen(const char *str);

char *itoa(char *dest, int n);
char *uitoa(char *dest, unsigned int n);
char *itohexa(char *dest, unsigned x);