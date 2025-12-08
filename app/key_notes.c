#include <stdbool.h>

#include <app.h>
#include <debug.h>
#include <keyboard.h>
#include <sound.h>

void key_notes_main()
{
    while (1) {
        char scancode = kbd_get_key(true).scancode;
        if (scancode == KEY_ESC) {
            break;
        }
        uint32_t freq = 250 + (scancode * 100);
        request_beep(freq);
    }
}