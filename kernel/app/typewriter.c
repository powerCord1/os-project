#include <stdbool.h>

#include <app.h>
#include <debug.h>
#include <keyboard.h>
#include <stdio.h>
#include <tty.h>

void typewriter_main()
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
            char c = scancode_map[(uint8_t)scancode];
            if (c == 0) {
                // if it's not in the map
                continue;
            }

            bool is_alpha = (c >= 'a' && c <= 'z');

            if (is_alpha && (kbd_modifiers.shift ^ kbd_modifiers.caps_lock)) {
                c = kbd_capitalise(c);
            }
            printf("%c", c);
            continue;
        }
        break;
    }
}