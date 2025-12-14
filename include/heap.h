#include <stddef.h>

#define HEAP_SIZE 1024 * 1024 // 1 MB

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void heap_init();
size_t heap_get_used_memory();