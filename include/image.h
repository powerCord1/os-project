#pragma once

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint16_t bit_depth;
    uint32_t size;
    void *data;
} image_t;

image_t *bmp_load(void *data);
void bmp_free(image_t *image);