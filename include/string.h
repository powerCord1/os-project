#include <stddef.h>

int memcmp(const void *, const void *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
char *strcpy(char *test, const char *src);
size_t strlen(const char *str);

char *itoa(char *dest, int n);
char *uitoa(char *dest, unsigned int n);
char *itohexa(char *dest, unsigned x);