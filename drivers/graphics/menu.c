#include <stddef.h>

#include <debug.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <menu.h>
#include <stdio.h>

void create_menu(const char *title, const char *prompt, menu_t *menu,
                 size_t item_count)
{
    while (1) {
        log_verbose("Refreshing menu: %s", title);
        fb_clear();
        fb_draw_title(title);
        printf("%s\n", prompt);
        for (size_t i = 0; i < item_count; i++) {
            printf("%d. %s\n", i + 1, menu[i].name);
        }

        key_t keypress = kbd_get_key(true);
        if (keypress.scancode == KEY_ESC) {
            break;
        }
        size_t choice_index = keypress.key - '1';
        if (choice_index > item_count - 1) {
            continue;
        } else {
            fb_clear();
            fb_draw_title(menu[choice_index].name);
            log_verbose("Launching app: %s", menu[choice_index].name);
            menu[choice_index].entry();
        }
    }
}