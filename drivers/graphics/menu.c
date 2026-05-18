#include <stddef.h>
#include <string.h>

#include <debug.h>
#include <font.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <limine_defs.h>
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
            } else {
                cur_index = item_count - 1;
            }
        } else if (keypress.scancode == KEY_ARROW_DOWN) {
            if (cur_index < item_count - 1) {
                cur_index++;
            } else {
                cur_index = 0;
            }
        } else if (keypress.scancode == KEY_ENTER) {
            fb_clear();
            fb_draw_title(menu[cur_index].name);
            log_verbose("Selected option: %s", menu[cur_index].name);
            if (menu[cur_index].entry != NULL) {
                menu[cur_index].entry();
            } else {
                log_warn("Not executing null entry: %s", menu[cur_index].name);
            }
        } else {
            goto wait_for_key;
        }
    }
}

void launch_popup(const char *message)
{
    uint32_t old_fg = fb_get_fg();
    uint32_t old_bg = fb_get_bg();

    size_t msg_len = strlen(message);
    uint32_t box_width = (msg_len + 4) * char_width;
    uint32_t box_height = 5 * char_height;

    uint32_t x = (fb->width - box_width) / 2;
    uint32_t y = (fb->height - box_height) / 2;

#if DRAW_POPUP_SHADOW
    fb_draw_rect(x + 4, y + 4, box_width, box_height, 0x444444);
#endif

    // Box background
    fb_draw_rect(x, y, box_width, box_height, old_fg);

    // Inner box
    fb_draw_rect(x + 2, y + 2, box_width - 4, box_height - 4, old_bg);

    fb_set_color(old_fg, old_bg);
    fb_set_cursor(x + 2 * char_width, y + 2 * char_height);
    printf("%s", message);
    fb_hide_cursor();

    while (1) {
        key_t keypress = kbd_get_key(true);
        if (keypress.scancode == KEY_ENTER || keypress.scancode == KEY_ESC ||
            keypress.scancode == KEY_SPACE) {
            break;
        }
    }

    fb_show_cursor();
    fb_set_color(old_fg, old_bg);
}