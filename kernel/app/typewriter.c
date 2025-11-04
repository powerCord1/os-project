#include <app.h>
#include <keyboard.h>
#include <stdio.h>

void typewriter_init()
{
    while (1) {
        char scancode;
        char last_char = kbd_get_last_char(true, &scancode);

        if (scancode == KEY_ESC) {
            break;
        } else {
            printf("%c", last_char);
        }
    }
}