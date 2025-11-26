#include <stdbool.h>

#include <app.h>
#include <cpu.h>
#include <keyboard.h>
#include <pit.h>
#include <stdio.h>

void pit_test_main()
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