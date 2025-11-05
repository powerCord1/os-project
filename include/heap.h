#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);
void heap_init(void);

#endif
