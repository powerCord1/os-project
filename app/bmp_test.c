#include <app.h>
#include <debug.h>
#include <framebuffer.h>
#include <image.h>
#include <keyboard.h>
#include <resource.h>
#include <stdio.h>

void bmp_test_main()
{
    image_t *image = bmp_load(test_bmp);
    if (!image) {
        log_err("Failed to load BMP image.\n");
        return;
    }

    fb_draw_image(image, 0, 0);
    bmp_free(image);
    while (kbd_get_key(true).scancode != KEY_ESC) {
    }
}