#include <stddef.h>
#include <stdint.h>

#include <app.h>
#include <cpu.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <limine_defs.h>
#include <pit.h>

void element_test()
{
    struct limine_framebuffer *fb = get_fb_data();

    // rect
    fb_draw_rect(fb->width / 2 - 150, 100, 300, 50, 0xff0000);

// line angles
#define clear true
#define radius 100
#define interval 10
    const uint32_t x = fb->width / 2;
    const uint32_t y = fb->height / 2;

    // top
    for (uint32_t i = x - radius; i <= x + radius; i++) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, i, y - radius);
        pit_wait_ms(interval);
    }

    // right
    for (uint32_t i = y - radius; i <= y + radius; i++) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, x + radius, i);
        pit_wait_ms(interval);
    }

    // bottom
    for (uint32_t i = x + radius; i > x - radius; i--) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, i, y + radius);
        pit_wait_ms(interval);
    }

    // left
    for (uint32_t i = y + radius; i > y - radius; i--) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, x - radius, i);
        pit_wait_ms(interval);
    }

    while (kbd_get_key(true).scancode != KEY_ESC) {
        halt();
    }
}