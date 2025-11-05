#include <stdbool.h>

#include <app.h>
#include <keyboard.h>
#include <pit.h>

void key_notes_main()
{
    while (1) {
        char scancode = kbd_get_scancode(true);
        if (scancode == KEY_ESC) {
            break;
        }
        pit_request_beep(250 + (scancode * 100));
    }
}