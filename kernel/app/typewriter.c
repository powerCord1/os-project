#include <stdbool.h>

#include <app.h>
#include <debug.h>
#include <keyboard.h>
#include <stdio.h>
#include <tty.h>

void typewriter_init()
{
    while (1) {
        char scancode = kbd_get_scancode(true);
        log_info("scancode: 0x%x", scancode);

        switch (scancode) {
        case KEY_ESC:
            break;
        case KEY_ARROW_LEFT:
            term_cursor_back(false);
            continue;
        case KEY_ARROW_RIGHT:
            term_cursor_forward();
            continue;
        case KEY_ARROW_UP:
            term_cursor_up();
            continue;
        case KEY_ARROW_DOWN:
            term_cursor_down();
            continue;
        default:
            printf("%c", scancode_map[(uint8_t)scancode]);
            continue;
        }
        break;
    }
}