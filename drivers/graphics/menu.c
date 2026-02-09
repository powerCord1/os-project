#include <stddef.h>

#include <debug.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <menu.h>
#include <stdio.h>

void create_menu(const char *title, const char *prompt, menu_t *menu,
                 size_t item_count)
{
    size_t cur_index = 0;
    uint32_t fg = fb_get_fg();
    uint32_t bg = fb_get_bg();
    while (1) {
        log_verbose("Refreshing menu: %s", title);
        fb_clear();
        fb_draw_title(title);
        printf("%s\n", prompt);
        for (size_t i = 0; i < item_count; i++) {
            if (i == cur_index) {
                fb_set_color(bg, fg);
            } else {
                fb_set_color(fg, bg);
            }
            printf("%s\n", menu[i].name);
        }

    wait_for_key:
        key_t keypress = kbd_get_key(true);
        if (keypress.scancode == KEY_ESC) {
            break;
        } else if (keypress.scancode == KEY_ARROW_UP) {
            if (cur_index > 0) {
                cur_index--;
            }
        } else if (keypress.scancode == KEY_ARROW_DOWN) {
            if (cur_index < item_count - 1) {
                cur_index++;
            }
        } else if (keypress.scancode == KEY_ENTER) {
            fb_clear();
            fb_draw_title(menu[cur_index].name);
            log_verbose("Selected option: %s", menu[cur_index].name);
            menu[cur_index].entry();
        } else {
            goto wait_for_key;
        }
    }
}