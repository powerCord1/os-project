#include <app.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <limine_defs.h>
#include <math.h>

void sin_test_main()
{
    struct limine_framebuffer *fb = get_fb_data();

    fb_clear();
    fb_draw_title("Sine Wave Test");

    double scale_x = fb->width / (2 * M_PI);
    double scale_y = fb->height / 2.0;
    double offset_y = fb->height / 2.0;

    for (int i = 0; i < fb->width; ++i) {
        double x = i / scale_x;
        double y = offset_y - sin(x) * scale_y / 2.0;
        if (y >= 0 && y < fb->height) {
            fb_put_pixel(i, (uint32_t)y, 0x00FF00);
        }
    }

    // Draw X and Y axes
    fb_draw_line(0, (uint32_t)offset_y, fb->width - 1,
                 (uint32_t)offset_y);                              // X-axis
    fb_draw_line(fb->width / 2, 0, fb->width / 2, fb->height - 1); // Y-axis

    while (kbd_get_key(true).scancode != KEY_ESC) {
    }
}