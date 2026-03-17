#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <app.h>
#include <array.h>
#include <font.h>
#include <framebuffer.h>
#include <keyboard.h>
#include <limine_defs.h>
#include <stdio.h>
#include <time.h>

typedef enum { SETTING_BOOL, SETTING_INT } setting_type_t;

typedef struct {
    const char *name;
    setting_type_t type;
    void *value;
    int min;
    int max;
} setting_t;

typedef enum { STATE_NAVIGATING, STATE_EDITING } app_state_t;

static int tz_offset;
static bool daylight_savings_enabled;
static bool _keyboard_logging_enabled;

static setting_t settings[] = {
    {"Timezone offset", SETTING_INT, &tz_offset, -12, 12},
    {"Daylight savings", SETTING_BOOL, &daylight_savings_enabled, 0, 0},
    {"Keyboard Logging", SETTING_BOOL, &_keyboard_logging_enabled, 0, 0},
};

#define SETTINGS_COUNT ARRAY_SIZE(settings)

static void get_vars()
{
    tz_offset = get_timezone();
    daylight_savings_enabled = get_daylight_savings();
    _keyboard_logging_enabled = keyboard_logging_enabled;
}

static void set_vars()
{
    set_timezone(tz_offset);
    set_daylight_savings(daylight_savings_enabled);
    keyboard_logging_enabled = _keyboard_logging_enabled;
}

static void render_settings(size_t selected_index, app_state_t state)
{
    uint32_t fg = fb_get_fg();
    uint32_t bg = fb_get_bg();

    fb_set_cursor(0, fb_viewport.y);

    for (size_t i = 0; i < SETTINGS_COUNT; i++) {
        bool is_selected = (i == selected_index);

        if (is_selected && state == STATE_NAVIGATING) {
            fb_set_color(bg, fg);
        } else {
            fb_set_color(fg, bg);
        }

        printf(" %s ", settings[i].name);
        fb_set_color(fg, bg);
        printf(": ");

        if (is_selected && state == STATE_EDITING) {
            fb_set_color(bg, fg);
        } else {
            fb_set_color(fg, bg);
        }

        if (settings[i].type == SETTING_BOOL) {
            printf("[%s]", *(bool *)settings[i].value ? "ON " : "OFF");
        } else if (settings[i].type == SETTING_INT) {
            printf("< %d >", *(int *)settings[i].value);
        }

        fb_set_color(fg, bg);
        printf("\n");
    }

    // Move cursor to the bottom of the screen for the footer
    struct limine_framebuffer *fb = get_fb_data();
    fb_set_cursor(0, fb->height - char_height);
}

void settings_main()
{
    get_vars();
    size_t selected_index = 0;
    app_state_t state = STATE_NAVIGATING;

    while (1) {
        fb_clear_vp();
        render_settings(selected_index, state);

        key_t key = kbd_get_key(true);

        if (state == STATE_NAVIGATING) {
            if (key.scancode == KEY_ARROW_UP) {
                if (selected_index > 0) {
                    selected_index--;
                } else {
                    selected_index = SETTINGS_COUNT - 1;
                }
            } else if (key.scancode == KEY_ARROW_DOWN) {
                if (selected_index < SETTINGS_COUNT - 1) {
                    selected_index++;
                } else {
                    selected_index = 0;
                }
            } else if (key.scancode == KEY_ENTER) {
                state = STATE_EDITING;
            } else if (key.scancode == KEY_ESC) {
                fb_clear();
                break;
            }
        } else if (state == STATE_EDITING) {
            if (key.scancode == KEY_ESC) {
                state = STATE_NAVIGATING;
            } else if (key.scancode == KEY_ENTER) {
                state = STATE_NAVIGATING;
            } else if (key.scancode == KEY_ARROW_UP) {
                if (settings[selected_index].type == SETTING_BOOL) {
                    *(bool *)settings[selected_index].value =
                        !*(bool *)settings[selected_index].value;
                } else if (settings[selected_index].type == SETTING_INT) {
                    if (*(int *)settings[selected_index].value <
                        settings[selected_index].max) {
                        (*(int *)settings[selected_index].value)++;
                    }
                }
            } else if (key.scancode == KEY_ARROW_DOWN) {
                if (settings[selected_index].type == SETTING_BOOL) {
                    *(bool *)settings[selected_index].value =
                        !*(bool *)settings[selected_index].value;
                } else if (settings[selected_index].type == SETTING_INT) {
                    if (*(int *)settings[selected_index].value >
                        settings[selected_index].min) {
                        (*(int *)settings[selected_index].value)--;
                    }
                }
            }
        }
    }
    set_vars();
}