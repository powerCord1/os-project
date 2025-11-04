#include <stdbool.h>

#include <app.h>
#include <keyboard.h>
#include <stdio.h>
#include <tty.h>

void typewriter_init()
{
    while (1) {
        char last_char = kbd_get_last_char(true);
        char scancode = kbd_get_last_scancode();

        switch (scancode) {
        case KEY_ESC:
            break;
        case KEY_ARROW_LEFT:
            term_cursor_back(false);
            continue;
        case KEY_ARROW_RIGHT:
            term_cursor_forward();
            continue;
        default:
            printf("%c", last_char);
            continue;
        }
        break;
    }
}