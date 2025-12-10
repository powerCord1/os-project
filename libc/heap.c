#include <stdbool.h>
#include <stdint.h>

#include <heap.h>
#include <stddef.h>
#include <string.h>

#define HEAP_SIZE 1024 * 1024 // 1 MB

static uint8_t heap[HEAP_SIZE];

typedef struct block {
    size_t size;
    bool free;
    struct block *next;
} block_t;

static block_t *heap_start = (void *)heap;

void heap_init()
{
    heap_start->size = HEAP_SIZE - sizeof(block_t);
    heap_start->free = true;
    heap_start->next = NULL;
}

void *malloc(size_t size)
{
    block_t *curr = heap_start;

    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size > size + sizeof(block_t)) {
                block_t *new_block =
                    (block_t *)((uint8_t *)curr + sizeof(block_t) + size);

                new_block->size = curr->size - size - sizeof(block_t);
                new_block->free = true;
                new_block->next = curr->next;
                curr->next = new_block;
                curr->size = size;
            }

            curr->free = false;
            return (void *)((uint8_t *)curr + sizeof(block_t));
        }
        curr = curr->next;
    }

    return NULL;
}

void free(void *ptr)
{
    if (!ptr) {
        return;
    }

    block_t *block_to_free = (block_t *)((uint8_t *)ptr - sizeof(block_t));

    block_to_free->free = true;

    if (block_to_free->next && block_to_free->next->free) {
        block_to_free->size += sizeof(block_t) + block_to_free->next->size;

        block_to_free->next = block_to_free->next->next;
    }
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return malloc(size);
    }

    block_t *block = (block_t *)((uint8_t *)ptr - sizeof(block_t));

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    if (block->size >= size) {
        // enough space is available, can return original pointer
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}