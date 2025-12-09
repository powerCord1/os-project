#include <stddef.h>
#include <stdint.h>

#include <debug.h>
#include <heap.h>
#include <image.h>
#include <string.h>

#define BMP_MAGIC 0x4D42

typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} __attribute__((packed)) bmp_header_t;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t size_image;
    int32_t x_pels_per_meter;
    int32_t y_pels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important
} __attribute__((packed)) bmp_info_header_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} __attribute__((packed)) bmp_color_t;

image_t *bmp_load(void *data)
{
    bmp_header_t *header = (bmp_header_t *)data;
    if (header->type != BMP_MAGIC) {
        log_err("BMP: Invalid magic number: %x", header->type);
        return NULL;
    }

    bmp_info_header_t *info_header =
        (bmp_info_header_t *)((uintptr_t)data + sizeof(bmp_header_t));

    if (info_header->bit_count != 8 && info_header->bit_count != 24 &&
        info_header->bit_count != 32) {
        log_err("BMP: Unsupported bit depth: %d", info_header->bit_count);
        return NULL;
    }

    image_t *image = (image_t *)malloc(sizeof(image_t));
    if (!image) {
        log_err("BMP: Failed to allocate memory for image_t");
        return NULL;
    }

    bool is_top_down = info_header->height < 0;
    image->width = info_header->width;
    image->height = is_top_down ? -info_header->height : info_header->height;
    image->bit_depth = 32; // convert all formats to 32-bit for the framebuffer

    uint32_t dest_row_size = image->width * (image->bit_depth / 8);
    uint32_t data_size = dest_row_size * image->height;
    image->size = data_size;

    image->data = malloc(data_size);
    if (!image->data) {
        log_err("BMP: Failed to allocate memory for pixel data");
        free(image);
        return NULL;
    }

    uint8_t *src_pixel_data = (uint8_t *)((uintptr_t)data + header->offset);

    uint32_t src_row_size =
        ((image->width * info_header->bit_count + 31) / 32) * 4;

    for (uint32_t y = 0; y < image->height; y++) {
        uint32_t src_y = is_top_down ? y : image->height - 1 - y;
        uint32_t *dest_row =
            (uint32_t *)((uintptr_t)image->data + y * dest_row_size);

        if (info_header->bit_count == 32) {
            uint8_t *src_row = src_pixel_data + src_y * src_row_size;
            memcpy(dest_row, src_row, image->width * 4);
        } else if (info_header->bit_count == 24) {
            uint8_t *src_row = src_pixel_data + src_y * src_row_size;
            for (uint32_t x = 0; x < image->width; x++) {
                uint8_t b = src_row[x * 3];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dest_row[x] = (r << 16) | (g << 8) | b;
            }
        } else if (info_header->bit_count == 8) {
            uint8_t *src_row = src_pixel_data + src_y * src_row_size;
            bmp_color_t *palette =
                (bmp_color_t *)((uintptr_t)info_header + info_header->size);
            for (uint32_t x = 0; x < image->width; x++) {
                uint8_t index = src_row[x];
                bmp_color_t color = palette[index];
                dest_row[x] =
                    (color.red << 16) | (color.green << 8) | color.blue;
            }
        }
    }

    log_info("BMP: Loaded image %dx%d, %d-bit", image->width, image->height,
             info_header->bit_count);

    return image;
}

void bmp_free(image_t *image)
{
    free(image->data);
    free(image);
}