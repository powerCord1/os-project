#include <app.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <limine_defs.h>
#include <math.h>
#include <pit.h>
#include <stdio.h>

void random_test_main()
{
    struct limine_framebuffer *fb = get_fb_data();

    // outputs noise to the framebuffer
    fb_clear();

    while (kbd_get_key(false).scancode != KEY_ESC) {
        for (size_t y = 0; y < fb->height; y++) {
            for (size_t x = 0; x < fb->width; x++) {
                fb_put_pixel(x, y, random_range(0, 0xFFFFFF));
            }
        }
        pit_wait_ms(1);
    }
}