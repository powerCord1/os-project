#include <stdbool.h>
#include <stdint.h>

#include <heap.h>
#include <lock.h>
#include <panic.h>
#include <stddef.h>
#include <string.h>

#define PAGE_SIZE       4096
#define PAGE_COUNT      (HEAP_SIZE / PAGE_SIZE)
#define SLAB_DATA_OFF   64
#define NUM_ZONES       7
#define ZONE_LARGE      0xFF
#define ZONE_LARGE_CONT 0xFE

static uint8_t heap[HEAP_SIZE] __attribute__((aligned(4096)));
static uint8_t page_info[PAGE_COUNT];
static spinlock_t heap_lock;

static const uint16_t zone_sizes[NUM_ZONES] = {
    32, 64, 128, 256, 512, 1024, 2048
};

typedef struct slab {
    struct slab *next;
    struct slab *prev;
    uint16_t freecount;
    uint8_t zone_idx;
    uint8_t items_per_slab;
    uint64_t bitmap[2];
} slab_t;

typedef struct {
    slab_t *partial;
    slab_t *full;
    uint16_t item_size;
    uint8_t items_per_slab;
} zone_t;

static zone_t zones[NUM_ZONES];

static inline int size_to_zone(size_t size)
{
    if (size <= 32)  return 0;
    if (size <= 64)  return 1;
    if (size <= 128) return 2;
    if (size <= 256) return 3;
    if (size <= 512) return 4;
    if (size <= 1024) return 5;
    if (size <= 2048) return 6;
    return -1;
}

static inline size_t ptr_to_page(void *ptr)
{
    return ((uint8_t *)ptr - heap) / PAGE_SIZE;
}

static void *page_alloc(size_t count)
{
    size_t run = 0;
    for (size_t i = 0; i < PAGE_COUNT; i++) {
        if (page_info[i] == 0) {
            run++;
            if (run == count) {
                size_t start = i + 1 - count;
                page_info[start] = ZONE_LARGE;
                for (size_t j = start + 1; j <= i; j++)
                    page_info[j] = ZONE_LARGE_CONT;
                return heap + start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return NULL;
}

static void page_free(void *ptr)
{
    size_t pg = ptr_to_page(ptr);
    if (pg >= PAGE_COUNT || page_info[pg] != ZONE_LARGE)
        return;
    page_info[pg] = 0;
    for (size_t i = pg + 1; i < PAGE_COUNT && page_info[i] == ZONE_LARGE_CONT; i++)
        page_info[i] = 0;
}

static size_t large_alloc_size(void *ptr)
{
    size_t pg = ptr_to_page(ptr);
    size_t count = 1;
    for (size_t i = pg + 1; i < PAGE_COUNT && page_info[i] == ZONE_LARGE_CONT; i++)
        count++;
    return count * PAGE_SIZE;
}

static inline int bitmap_ffs(uint64_t *bitmap, int bits)
{
    int words = (bits + 63) / 64;
    for (int w = 0; w < words; w++) {
        if (bitmap[w]) {
            int bit = w * 64 + __builtin_ctzll(bitmap[w]);
            return bit < bits ? bit : -1;
        }
    }
    return -1;
}

static inline void bitmap_set(uint64_t *bitmap, int bit)
{
    bitmap[bit / 64] |= (1ULL << (bit % 64));
}

static inline void bitmap_clear(uint64_t *bitmap, int bit)
{
    bitmap[bit / 64] &= ~(1ULL << (bit % 64));
}

static inline bool bitmap_test(uint64_t *bitmap, int bit)
{
    return (bitmap[bit / 64] >> (bit % 64)) & 1;
}

static void slab_list_remove(slab_t **head, slab_t *s)
{
    if (s->prev)
        s->prev->next = s->next;
    else
        *head = s->next;
    if (s->next)
        s->next->prev = s->prev;
    s->next = s->prev = NULL;
}

static void slab_list_push(slab_t **head, slab_t *s)
{
    s->prev = NULL;
    s->next = *head;
    if (*head)
        (*head)->prev = s;
    *head = s;
}

static slab_t *slab_create(int zone_idx)
{
    size_t pg;
    for (pg = 0; pg < PAGE_COUNT; pg++) {
        if (page_info[pg] == 0)
            break;
    }
    if (pg == PAGE_COUNT)
        return NULL;

    page_info[pg] = zone_idx + 1;
    slab_t *s = (slab_t *)(heap + pg * PAGE_SIZE);
    memset(s, 0, sizeof(*s));
    s->zone_idx = zone_idx;
    s->items_per_slab = zones[zone_idx].items_per_slab;
    s->freecount = s->items_per_slab;

    int bits = s->items_per_slab;
    int full_words = bits / 64;
    for (int w = 0; w < full_words; w++)
        s->bitmap[w] = ~0ULL;
    int rem = bits % 64;
    if (rem)
        s->bitmap[full_words] = (1ULL << rem) - 1;

    return s;
}

void heap_init()
{
    memset(page_info, 0, sizeof(page_info));
    memset(&heap_lock, 0, sizeof(heap_lock));

    for (int i = 0; i < NUM_ZONES; i++) {
        zones[i].item_size = zone_sizes[i];
        zones[i].items_per_slab = (PAGE_SIZE - SLAB_DATA_OFF) / zone_sizes[i];
        zones[i].partial = NULL;
        zones[i].full = NULL;
    }
}

void *malloc(size_t size)
{
    if (size == 0)
        size = 1;

    spinlock_acquire(&heap_lock);

    int zi = size_to_zone(size);
    if (zi < 0) {
        size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        void *p = page_alloc(pages);
        spinlock_release(&heap_lock);
        return p;
    }

    zone_t *z = &zones[zi];
    slab_t *s = z->partial;
    if (!s) {
        s = slab_create(zi);
        if (!s) {
            spinlock_release(&heap_lock);
            return NULL;
        }
        slab_list_push(&z->partial, s);
    }

    int bit = bitmap_ffs(s->bitmap, s->items_per_slab);
    if (bit < 0) {
        spinlock_release(&heap_lock);
        return NULL;
    }

    bitmap_clear(s->bitmap, bit);
    s->freecount--;

    if (s->freecount == 0) {
        slab_list_remove(&z->partial, s);
        slab_list_push(&z->full, s);
    }

    void *ptr = (uint8_t *)s + SLAB_DATA_OFF + bit * z->item_size;
    spinlock_release(&heap_lock);
    return ptr;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    spinlock_acquire(&heap_lock);

    size_t pg = ptr_to_page(ptr);
    if (pg >= PAGE_COUNT) {
        spinlock_release(&heap_lock);
        return;
    }

    uint8_t info = page_info[pg];
    if (info == ZONE_LARGE || info == ZONE_LARGE_CONT) {
        page_free(ptr);
        spinlock_release(&heap_lock);
        return;
    }

    slab_t *s = (slab_t *)(heap + pg * PAGE_SIZE);
    uint8_t zi = s->zone_idx;
    zone_t *z = &zones[zi];

    int bit = ((uint8_t *)ptr - ((uint8_t *)s + SLAB_DATA_OFF)) / z->item_size;
    if (bit < 0 || bit >= s->items_per_slab) {
        spinlock_release(&heap_lock);
        return;
    }

    bitmap_set(s->bitmap, bit);
    bool was_full = (s->freecount == 0);
    s->freecount++;

    if (was_full) {
        slab_list_remove(&z->full, s);
        slab_list_push(&z->partial, s);
    }

    if (s->freecount == s->items_per_slab) {
        slab_list_remove(&z->partial, s);
        page_info[pg] = 0;
    }

    spinlock_release(&heap_lock);
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    size_t old_size;
    size_t pg = ptr_to_page(ptr);
    uint8_t info = page_info[pg];

    if (info == ZONE_LARGE || info == ZONE_LARGE_CONT) {
        old_size = large_alloc_size(ptr);
    } else {
        slab_t *s = (slab_t *)(heap + pg * PAGE_SIZE);
        old_size = zones[s->zone_idx].item_size;
    }

    if (size <= old_size) {
        int new_zi = size_to_zone(size);
        int old_zi = size_to_zone(old_size);
        if (new_zi >= 0 && old_zi >= 0 && new_zi == old_zi)
            return ptr;
        if (new_zi < 0 && old_zi < 0)
            return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    size_t copy = old_size < size ? old_size : size;
    memcpy(new_ptr, ptr, copy);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void abort(void)
{
    panic("abort");
}

size_t heap_get_used_memory()
{
    size_t used = 0;
    spinlock_acquire(&heap_lock);

    for (size_t pg = 0; pg < PAGE_COUNT; pg++) {
        uint8_t info = page_info[pg];
        if (info == ZONE_LARGE) {
            size_t count = 1;
            while (pg + count < PAGE_COUNT && page_info[pg + count] == ZONE_LARGE_CONT)
                count++;
            used += count * PAGE_SIZE;
            pg += count - 1;
        } else if (info >= 1 && info <= NUM_ZONES) {
            slab_t *s = (slab_t *)(heap + pg * PAGE_SIZE);
            zone_t *z = &zones[s->zone_idx];
            used += (s->items_per_slab - s->freecount) * z->item_size;
        }
    }

    spinlock_release(&heap_lock);
    return used;
}
