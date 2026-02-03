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
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strtok(char *str, const char *delim);
size_t strspn(const char *str, const char *accept);
char *strpbrk(const char *str, const char *accept);

int tolower(int c);
int toupper(int c);

char *itohexa(char *dest, unsigned x);
char *itoa(char *dest, int n);
char *uitoa(char *dest, unsigned int n);
int atoi(const char *str);