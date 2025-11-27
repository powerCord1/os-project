#include <stdbool.h>

#include <app.h>
#include <debug.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <stdio.h>

void typewriter_main()
{
    while (1) {
        key_t key = kbd_get_key(true);
        log_info("scancode: 0x%x", key.scancode);

        switch (key.scancode) {
        case KEY_ESC:
            return;
        case KEY_ARROW_LEFT:
            fb_cursor_left();
            ;
            continue;
        case KEY_ARROW_RIGHT:
            fb_cursor_right();
            continue;
        case KEY_ARROW_UP:
            fb_cursor_up();
            continue;
        case KEY_ARROW_DOWN:
            fb_cursor_down();
            continue;
        default:
            char c = key.key;
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
    }
}