#include <stdbool.h>
#include <stdint.h>

#include <heap.h>
#include <stddef.h>

#define HEAP_SIZE 1024 * 1024 // 1 MB heap

static uint8_t heap[HEAP_SIZE];

typedef struct block {
    size_t size;
    bool free;
    struct block *next;
} block_t;

static block_t *free_list = (void *)heap;

void heap_init()
{
    free_list->size = HEAP_SIZE - sizeof(block_t);
    free_list->free = true;
    free_list->next = NULL;
}

void *malloc(size_t size)

{
    block_t *curr = free_list;

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

    // Merge with next block if it's free

    if (block_to_free->next && block_to_free->next->free) {
        block_to_free->size += sizeof(block_t) + block_to_free->next->size;

        block_to_free->next = block_to_free->next->next;
    }
}
