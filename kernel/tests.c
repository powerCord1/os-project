#include <stddef.h>

#include <acpi.h>
#include <apic.h>
#include <cpu.h>
#include <debug.h>
#include <framebuffer.h>
#include <heap.h>
#include <image.h>
#include <interrupts.h>
#include <keyboard.h>
#include <limine_defs.h>
#include <math.h>
#include <menu.h>
#include <pit.h>
#include <resource.h>
#include <scheduler.h>
#include <sound.h>
#include <stdio.h>
#include <string.h>
#include <tests.h>
#include <timer.h>

thread_t *thread1;
thread_t *thread2;

void thread_test()
{
    thread1 = thread_create(test_thread1, NULL);
    thread2 = thread_create(test_thread2, NULL);
}

void cancel_test_threads()
{
    thread_cancel(thread1->id);
    thread_cancel(thread2->id);
}

void test_thread1(void *arg)
{
    (void)arg;
    while (1) {
        log_info("Thread 1 running");
        wait_ms(1000);
    }
}

void test_thread2(void *arg)
{
    (void)arg;
    while (1) {
        log_info("Thread 2 running");
        wait_ms(1000);
    }
}

void bmp_test()
{
    image_t *image = bmp_load(test_bmp);
    if (!image) {
        log_err("Failed to load BMP image");
        return;
    }

    fb_draw_image(image, 0, 0);
    bmp_free(image);
    kbd_wait_for_esc();
}

void element_test()
{
    struct limine_framebuffer *fb = get_fb_data();

    // rect
    fb_draw_rect(fb->width / 2 - 150, 100, 300, 50, 0xff0000);

    // circles
    fb_draw_circle(fb->width / 2 - 300, fb->height / 3 * 2 + 100, 100, 0x00ff00,
                   true);
    fb_draw_circle(fb->width / 2 + 300, fb->height / 3 * 2 + 100, 100, 0x00ff00,
                   false);

    // line
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
        wait_ms(interval);
    }

    // right
    for (uint32_t i = y - radius; i <= y + radius; i++) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, x + radius, i);
        wait_ms(interval);
    }

    // bottom
    for (uint32_t i = x + radius; i > x - radius; i--) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, i, y + radius);
        wait_ms(interval);
    }

    // left
    for (uint32_t i = y + radius; i > y - radius; i--) {
#if clear
        fb_clear_region(x - radius, y - radius, x + radius + 1, y + radius + 1);
#endif
        fb_draw_line(x, y, x - radius, i);
        wait_ms(interval);
    }

    kbd_wait_for_esc();
}

void ssp_test()
{
    printf("Smashing the stack...\n");
    char buffer[10];
    memset(buffer, 'A', 100);
    printf("Stack killed successfully");
}

void random_test()
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
        wait_ms(1);
    }
}

void colour_test()
{
    fb_rgb_test();
    kbd_wait_for_esc();
}

void sin_test()
{
    struct limine_framebuffer *fb = get_fb_data();

    fb_clear();
    fb_draw_title("Sine Wave Test");

    double scale_x = fb->width / (2 * M_PI);
    double scale_y = fb->height / 2.0;
    double offset_y = fb->height / 2.0;

    for (uint64_t i = 0; i < fb->width; ++i) {
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

    kbd_wait_for_esc();
}

void pit_test()
{
    uint64_t last_check_ticks = pit_ticks;
    uint64_t seconds = 0;
    while (true) {
        if (pit_ticks >= last_check_ticks + 1000) {
            seconds++;
            last_check_ticks = pit_ticks;
            printf("%lu\r", seconds);
        }
        if (kbd_get_key(false).scancode == KEY_ESC) {
            break;
        }

        idle(); // wait for an interrupt
    }
}

void heap_test()
{
    printf("Running heap test...\n");

    char *test_str = (char *)malloc(20);
    if (test_str) {
        strcpy(test_str, "Heap test successful!");
        printf("%s\n", test_str);
        free(test_str);
    } else {
        printf("Failed to allocate memory.\n");
    }
    printf("Press any key exit\n");
    kbd_get_key(true);
}

void sound_test()
{
    for (uint16_t i = 0; i < 20000; i++) {
        if (kbd_get_key(false).scancode == KEY_ESC) {
            break;
        }
        play_sound(i);
        wait_ns(100);
    }
    nosound();
}

void page_fault_test()
{
    __asm__ volatile("mov $0xDEADBEEF, %rbx\n"
                     "movb $0xFF, (%rbx)\n");
}

void invalid_opcode_test()
{
    __asm__ volatile("ud2");
}

void list_acpi_devices()
{
    acpi_list_acpi_devices();
    kbd_wait_for_esc();
}

void apic_test()
{
    printf("APIC Test\n");
    printf("apic_in_use: %s\n", is_apic_in_use() ? "true" : "false");

    if (!is_apic_in_use()) {
        printf("APIC is not initialized. Test skipped.\n");
        kbd_wait_for_esc();
        return;
    }

    uint32_t id = lapic_read(LAPIC_ID);
    uint32_t ver = lapic_read(LAPIC_VER);

    printf("LAPIC ID: 0x%08x\n", id);
    printf("LAPIC Version: 0x%08x\n", ver);

    printf("Verifying PIT interrupts through APIC...\n");
    uint64_t start_ticks = pit_ticks;
    wait_ms(100);
    uint64_t end_ticks = pit_ticks;

    if (end_ticks > start_ticks) {
        printf("SUCCESS: PIT interrupts received (%lu ticks in 100ms)\n",
               end_ticks - start_ticks);
    } else {
        printf("FAILURE: No PIT interrupts received!\n");
    }

    kbd_wait_for_esc();
}