#include <stdbool.h>

#include <app.h>
#include <keyboard.h>
#include <pit.h>
#include <stdio.h>

void pit_test_main()
{
    while (1) {
        if (kbd_get_key(true).scancode == KEY_ESC) {
            break;
        }
    }
}